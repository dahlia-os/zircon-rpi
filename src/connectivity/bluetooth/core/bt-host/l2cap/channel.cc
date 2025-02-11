// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel.h"

#include <lib/async/default.h>
#include <lib/trace/event.h>
#include <zircon/assert.h>

#include <functional>
#include <memory>

#include "logical_link.h"
#include "src/connectivity/bluetooth/core/bt-host/common/log.h"
#include "src/connectivity/bluetooth/core/bt-host/common/run_or_post.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/basic_mode_rx_engine.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/basic_mode_tx_engine.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/enhanced_retransmission_mode_engines.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/l2cap.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace bt {
namespace l2cap {

Channel::Channel(ChannelId id, ChannelId remote_id, hci::Connection::LinkType link_type,
                 hci::ConnectionHandle link_handle, ChannelInfo info)
    : id_(id),
      remote_id_(remote_id),
      link_type_(link_type),
      link_handle_(link_handle),
      info_(info) {
  ZX_DEBUG_ASSERT(id_);
  ZX_DEBUG_ASSERT(link_type_ == hci::Connection::LinkType::kLE ||
                  link_type_ == hci::Connection::LinkType::kACL);
}

namespace internal {

fbl::RefPtr<ChannelImpl> ChannelImpl::CreateFixedChannel(ChannelId id,
                                                         fxl::WeakPtr<internal::LogicalLink> link) {
  // A fixed channel's endpoints have the same local and remote identifiers.
  // Setting the ChannelInfo MTU to kMaxMTU effectively cancels any L2CAP-level MTU enforcement for
  // services which operate over fixed channels. Such services often define minimum MTU values in
  // their specification, so they are required to respect these MTUs internally by:
  //   1.) never sending packets larger than their spec-defined MTU.
  //   2.) handling inbound PDUs which are larger than their spec-defined MTU appropriately.
  return fbl::AdoptRef(new ChannelImpl(id, id, link, ChannelInfo::MakeBasicMode(kMaxMTU, kMaxMTU)));
}

fbl::RefPtr<ChannelImpl> ChannelImpl::CreateDynamicChannel(ChannelId id, ChannelId peer_id,
                                                           fxl::WeakPtr<internal::LogicalLink> link,
                                                           ChannelInfo info) {
  return fbl::AdoptRef(new ChannelImpl(id, peer_id, link, info));
}

ChannelImpl::ChannelImpl(ChannelId id, ChannelId remote_id,
                         fxl::WeakPtr<internal::LogicalLink> link, ChannelInfo info)
    : Channel(id, remote_id, link->type(), link->handle(), info),
      active_(false),
      dispatcher_(nullptr),
      link_(link) {
  ZX_ASSERT(link_);
  ZX_ASSERT_MSG(
      info_.mode == ChannelMode::kBasic || info_.mode == ChannelMode::kEnhancedRetransmission,
      "Channel constructed with unsupported mode: %hhu\n", info.mode);

  // B-frames for Basic Mode contain only an "Information payload" (v5.0 Vol 3, Part A, Sec 3.1)
  FrameCheckSequenceOption fcs_option = info_.mode == ChannelMode::kEnhancedRetransmission
                                            ? FrameCheckSequenceOption::kIncludeFcs
                                            : FrameCheckSequenceOption::kNoFcs;
  auto send_cb = [rid = remote_id, link, fcs_option](auto pdu) {
    async::PostTask(link->dispatcher(), [=, pdu = std::move(pdu)] {
      if (link) {
        // |link| is expected to ignore this call and drop the packet if it has been closed.
        link->SendFrame(rid, *pdu, fcs_option);
      }
    });
  };

  if (info_.mode == ChannelMode::kBasic) {
    rx_engine_ = std::make_unique<BasicModeRxEngine>();
    tx_engine_ = std::make_unique<BasicModeTxEngine>(id, max_tx_sdu_size(), send_cb);
  } else {
    // Must capture |link| and not |link_| to avoid having to take |mutex_|.
    auto connection_failure_cb = [this, link] {
      ZX_ASSERT(thread_checker_.IsCreationThreadCurrent());

      // This isn't called until Channel has been activated and the callback is destroyed by
      // Deactivate, so even without taking |mutex_| we know that the channel is active. However,
      // this may be called from a locked context in HandleRxPdu, so defer the signal to after the
      // critical section so that we don't deadlock when removing this channel.
      async::PostTask(async_get_default_dispatcher(), [link] {
        if (link) {
          // |link| is expected to ignore this call if it has been closed.
          link->SignalError();
        }
      });
    };
    std::tie(rx_engine_, tx_engine_) = MakeLinkedEnhancedRetransmissionModeEngines(
        id, max_tx_sdu_size(), info_.max_transmissions, info_.n_frames_in_tx_window, send_cb,
        std::move(connection_failure_cb));
  }
}

const sm::SecurityProperties ChannelImpl::security() {
  std::lock_guard lock(mtx_);
  if (link_) {
    return link_->security();
  }
  return sm::SecurityProperties();
}

bool ChannelImpl::ActivateWithDispatcher(RxCallback rx_callback, ClosedCallback closed_callback,
                                         async_dispatcher_t* dispatcher) {
  ZX_DEBUG_ASSERT(rx_callback);
  ZX_DEBUG_ASSERT(closed_callback);

  fit::closure task;
  bool run_task = false;

  {
    std::lock_guard lock(mtx_);

    // Activating on a closed link has no effect. We also clear this on
    // deactivation to prevent a channel from being activated more than once.
    if (!link_)
      return false;

    ZX_DEBUG_ASSERT(!active_);
    active_ = true;
    ZX_DEBUG_ASSERT(!dispatcher_);
    dispatcher_ = dispatcher;
    rx_cb_ = std::move(rx_callback);
    closed_cb_ = std::move(closed_callback);

    // Route the buffered packets.
    if (!pending_rx_sdus_.empty()) {
      run_task = true;
      task = [func = rx_cb_.share(), pending = std::move(pending_rx_sdus_)]() mutable {
        TRACE_DURATION("bluetooth", "ChannelImpl::ActivateWithDispatcher pending drain");
        while (!pending.empty()) {
          TRACE_FLOW_END("bluetooth", "ChannelImpl::HandleRxPdu queued", pending.size());
          func(std::move(pending.front()));
          pending.pop();
        }
      };
      ZX_DEBUG_ASSERT(pending_rx_sdus_.empty());
    }
  }

  if (run_task) {
    RunOrPost(std::move(task), dispatcher);
  }

  return true;
}

bool ChannelImpl::ActivateOnDataDomain(RxCallback rx_callback, ClosedCallback closed_callback) {
  return ActivateWithDispatcher(std::move(rx_callback), std::move(closed_callback), nullptr);
}

void ChannelImpl::Deactivate() {
  std::lock_guard lock(mtx_);
  bt_log(TRACE, "l2cap", "deactivating channel (link: %#.4x, id: %#.4x)", link_handle(), id());

  // De-activating on a closed link has no effect.
  if (!link_ || !active_) {
    link_ = nullptr;
    return;
  }

  active_ = false;
  dispatcher_ = nullptr;
  rx_cb_ = {};
  closed_cb_ = {};
  rx_engine_ = {};
  tx_engine_ = {};

  // Tell the link to release this channel on its thread.
  async::PostTask(link_->dispatcher(), [this, link = link_] {
    if (link) {
      // |link| is expected to ignore this call if it has been closed.
      link->RemoveChannel(this);
    }
  });

  link_ = nullptr;
}

void ChannelImpl::SignalLinkError() {
  std::lock_guard lock(mtx_);

  // Cannot signal an error on a closed or deactivated link.
  if (!link_ || !active_)
    return;

  async::PostTask(async_get_default_dispatcher(), [link = link_] {
    if (link) {
      // |link| is expected to ignore this call if it has been closed.
      link->SignalError();
    }
  });
}

bool ChannelImpl::Send(ByteBufferPtr sdu) {
  ZX_DEBUG_ASSERT(sdu);

  std::lock_guard lock(mtx_);
  TRACE_DURATION("bluetooth", "l2cap:channel_send", "handle", link_->handle(), "id", id());

  if (!link_) {
    bt_log(ERROR, "l2cap", "cannot send SDU on a closed link");
    return false;
  }

  // Drop the packet if the channel is inactive.
  if (!active_)
    return false;

  return tx_engine_->QueueSdu(std::move(sdu));
}

void ChannelImpl::UpgradeSecurity(sm::SecurityLevel level, sm::StatusCallback callback,
                                  async_dispatcher_t* dispatcher) {
  ZX_ASSERT(callback);
  ZX_ASSERT(dispatcher);

  std::lock_guard lock(mtx_);

  if (!link_ || !active_) {
    bt_log(DEBUG, "l2cap", "Ignoring security request on inactive channel");
    return;
  }

  async::PostTask(link_->dispatcher(),
                  [link = link_, level, callback = std::move(callback), dispatcher]() mutable {
                    if (link) {
                      link->UpgradeSecurity(level, std::move(callback), dispatcher);
                    }
                  });
}

void ChannelImpl::OnClosed() {
  async_dispatcher_t* dispatcher;
  fit::closure task;

  {
    std::lock_guard lock(mtx_);
    bt_log(TRACE, "l2cap", "channel closed (link: %#.4x, id: %#.4x)", link_handle(), id());

    if (!link_ || !active_) {
      link_ = nullptr;
      return;
    }

    ZX_DEBUG_ASSERT(closed_cb_);
    dispatcher = dispatcher_;
    task = std::move(closed_cb_);
    active_ = false;
    link_ = nullptr;
    dispatcher_ = nullptr;
    rx_engine_ = nullptr;
    tx_engine_ = nullptr;
  }

  RunOrPost(std::move(task), dispatcher);
}

void ChannelImpl::HandleRxPdu(PDU&& pdu) {
  async_dispatcher_t* dispatcher;
  fit::closure task;

  {
    std::lock_guard lock(mtx_);
    TRACE_DURATION("bluetooth", "ChannelImpl::HandleRxPdu", "handle", link_->handle(), "channel_id",
                   id_);

    // link_ may be nullptr if a pdu is received after the channel has been deactivated but
    // before LogicalLink::RemoveChannel has been dispatched
    if (!link_) {
      bt_log(TRACE, "l2cap", "ignoring pdu on deactivated channel");
      return;
    }

    ZX_DEBUG_ASSERT(rx_engine_);

    ByteBufferPtr sdu = rx_engine_->ProcessPdu(std::move(pdu));
    if (!sdu) {
      // The PDU may have been invalid, out-of-sequence, or part of a segmented
      // SDU.
      // * If invalid, we drop the PDU (per Core Spec Ver 5, Vol 3, Part A,
      //   Secs. 3.3.6 and/or 3.3.7).
      // * If out-of-sequence or part of a segmented SDU, we expect that some
      //   later call to ProcessPdu() will return us an SDU containing this
      //   PDU's data.
      return;
    }

    // Buffer the packets if the channel hasn't been activated.
    if (!active_) {
      pending_rx_sdus_.emplace(std::move(sdu));
      // Tracing: we assume pending_rx_sdus_ is only filled once and use the length of queue
      // for trace ids.
      TRACE_FLOW_BEGIN("bluetooth", "ChannelImpl::HandleRxPdu queued", pending_rx_sdus_.size());
      return;
    }

    trace_flow_id_t trace_id = TRACE_NONCE();
    TRACE_FLOW_BEGIN("bluetooth", "ChannelImpl::HandleRxPdu callback", trace_id);

    dispatcher = dispatcher_;
    task = [func = rx_cb_.share(), sdu = std::move(sdu), trace_id]() mutable {
      TRACE_DURATION("bluetooth", "ChannelImpl::HandleRxPdu callback task");
      TRACE_FLOW_END("bluetooth", "ChannelImpl::HandleRxPdu callback", trace_id);
      func(std::move(sdu));
    };

    ZX_DEBUG_ASSERT(rx_cb_);
  }
  RunOrPost(std::move(task), dispatcher);
}

}  // namespace internal
}  // namespace l2cap
}  // namespace bt
