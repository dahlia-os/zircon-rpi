// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bredr_discovery_manager.h"

#include <lib/async/default.h>
#include <lib/fit/defer.h>
#include <zircon/assert.h>

#include "src/connectivity/bluetooth/core/bt-host/common/log.h"
#include "src/connectivity/bluetooth/core/bt-host/common/supplement_data.h"
#include "src/connectivity/bluetooth/core/bt-host/gap/peer_cache.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/transport.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/util.h"

namespace bt {
namespace gap {

namespace {

template <typename EventParamType, typename ResultType>
std::unordered_set<Peer*> ProcessInquiryResult(PeerCache* cache, const hci::EventPacket& event) {
  std::unordered_set<Peer*> updated;
  bt_log(TRACE, "gap-bredr", "inquiry result received");

  size_t result_size = event.view().payload_size() - sizeof(EventParamType);
  if ((result_size % sizeof(ResultType)) != 0) {
    bt_log(INFO, "gap-bredr", "ignoring wrong size result (%zu %% %zu != 0)", result_size,
           sizeof(ResultType));
    return updated;
  }

  const auto& result = event.params<EventParamType>();
  for (int i = 0; i < result.num_responses; i++) {
    DeviceAddress addr(DeviceAddress::Type::kBREDR, result.responses[i].bd_addr);
    Peer* peer = cache->FindByAddress(addr);
    if (!peer) {
      peer = cache->NewPeer(addr, true);
    }
    ZX_DEBUG_ASSERT(peer);

    peer->MutBrEdr().SetInquiryData(result.responses[i]);
    updated.insert(peer);
  }
  return updated;
}

}  // namespace

BrEdrDiscoverySession::BrEdrDiscoverySession(fxl::WeakPtr<BrEdrDiscoveryManager> manager)
    : manager_(manager) {}

BrEdrDiscoverySession::~BrEdrDiscoverySession() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  manager_->RemoveDiscoverySession(this);
}

void BrEdrDiscoverySession::NotifyDiscoveryResult(const Peer& peer) const {
  if (peer_found_callback_) {
    peer_found_callback_(peer);
  }
}

void BrEdrDiscoverySession::NotifyError() const {
  if (error_callback_) {
    error_callback_();
  }
}

BrEdrDiscoverableSession::BrEdrDiscoverableSession(fxl::WeakPtr<BrEdrDiscoveryManager> manager)
    : manager_(manager) {}

BrEdrDiscoverableSession::~BrEdrDiscoverableSession() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  manager_->RemoveDiscoverableSession(this);
}

BrEdrDiscoveryManager::BrEdrDiscoveryManager(fxl::WeakPtr<hci::Transport> hci,
                                             hci::InquiryMode mode, PeerCache* peer_cache)
    : hci_(std::move(hci)),
      dispatcher_(async_get_default_dispatcher()),
      cache_(peer_cache),
      result_handler_id_(0u),
      desired_inquiry_mode_(mode),
      current_inquiry_mode_(hci::InquiryMode::kStandard),
      weak_ptr_factory_(this) {
  ZX_DEBUG_ASSERT(cache_);
  ZX_DEBUG_ASSERT(hci_);
  ZX_DEBUG_ASSERT(dispatcher_);

  result_handler_id_ = hci_->command_channel()->AddEventHandler(
      hci::kInquiryResultEventCode, fit::bind_member(this, &BrEdrDiscoveryManager::InquiryResult));
  ZX_DEBUG_ASSERT(result_handler_id_);
  rssi_handler_id_ = hci_->command_channel()->AddEventHandler(
      hci::kInquiryResultWithRSSIEventCode,
      fbl::BindMember(this, &BrEdrDiscoveryManager::InquiryResult));
  ZX_DEBUG_ASSERT(rssi_handler_id_);
  eir_handler_id_ = hci_->command_channel()->AddEventHandler(
      hci::kExtendedInquiryResultEventCode,
      fbl::BindMember(this, &BrEdrDiscoveryManager::ExtendedInquiryResult));
  ZX_DEBUG_ASSERT(eir_handler_id_);

  // Set the Inquiry Scan Settings
  WriteInquiryScanSettings(kInquiryScanInterval, kInquiryScanWindow, true);
}

BrEdrDiscoveryManager::~BrEdrDiscoveryManager() {
  hci_->command_channel()->RemoveEventHandler(eir_handler_id_);
  hci_->command_channel()->RemoveEventHandler(rssi_handler_id_);
  hci_->command_channel()->RemoveEventHandler(result_handler_id_);
  InvalidateDiscoverySessions();
}

void BrEdrDiscoveryManager::RequestDiscovery(DiscoveryCallback callback) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(callback);

  bt_log(DEBUG, "gap-bredr", "RequestDiscovery");

  // If we're already waiting on a callback, then scanning is already starting.
  // Queue this to create a session when the scanning starts.
  if (!pending_discovery_.empty()) {
    bt_log(DEBUG, "gap-bredr", "discovery starting, add to pending");
    pending_discovery_.push(std::move(callback));
    return;
  }

  // If we're already scanning, just add a session.
  if (!discovering_.empty() || !zombie_discovering_.empty()) {
    bt_log(DEBUG, "gap-bredr", "add to active sessions");
    auto session = AddDiscoverySession();
    callback(hci::Status(), std::move(session));
    return;
  }

  pending_discovery_.push(std::move(callback));
  MaybeStartInquiry();
}

// Starts the inquiry procedure if any sessions exist or are waiting to start.
void BrEdrDiscoveryManager::MaybeStartInquiry() {
  if (pending_discovery_.empty() && discovering_.empty()) {
    bt_log(DEBUG, "gap-bredr", "no sessions, not starting inquiry");
    return;
  }

  bt_log(TRACE, "gap-bredr", "starting inquiry");

  auto self = weak_ptr_factory_.GetWeakPtr();
  if (desired_inquiry_mode_ != current_inquiry_mode_) {
    auto packet =
        hci::CommandPacket::New(hci::kWriteInquiryMode, sizeof(hci::WriteInquiryModeCommandParams));
    packet->mutable_payload<hci::WriteInquiryModeCommandParams>()->inquiry_mode =
        desired_inquiry_mode_;
    hci_->command_channel()->SendCommand(
        std::move(packet), [self, mode = desired_inquiry_mode_](auto, const auto& event) {
          if (!self) {
            return;
          }

          if (!hci_is_error(event, ERROR, "gap-bredr", "write inquiry mode failed")) {
            self->current_inquiry_mode_ = mode;
          }
        });
  }

  auto inquiry = hci::CommandPacket::New(hci::kInquiry, sizeof(hci::InquiryCommandParams));
  auto params = inquiry->mutable_payload<hci::InquiryCommandParams>();
  params->lap = hci::kGIAC;
  params->inquiry_length = kInquiryLengthDefault;
  params->num_responses = 0;
  hci_->command_channel()->SendExclusiveCommand(
      std::move(inquiry),
      [self](auto, const auto& event) {
        if (!self) {
          return;
        }
        auto status = event.ToStatus();
        if (bt_is_error(status, WARN, "gap-bredr", "inquiry error")) {
          // Failure of some kind, signal error to the sessions.
          self->InvalidateDiscoverySessions();

          // Fallthrough for callback to pending sessions.
        }

        // Resolve the request if the controller sent back a Command Complete or
        // Status event.
        // TODO(NET-770): Make it impossible for Command Complete to happen here
        // and remove handling for it.
        if (event.event_code() == hci::kCommandStatusEventCode ||
            event.event_code() == hci::kCommandCompleteEventCode) {
          // Inquiry started, make sessions for our waiting callbacks.
          while (!self->pending_discovery_.empty()) {
            auto callback = std::move(self->pending_discovery_.front());
            self->pending_discovery_.pop();
            callback(status, (status ? self->AddDiscoverySession() : nullptr));
          }
          return;
        }

        ZX_DEBUG_ASSERT(event.event_code() == hci::kInquiryCompleteEventCode);
        self->zombie_discovering_.clear();

        if (bt_is_error(status, TRACE, "gap", "inquiry complete error")) {
          return;
        }

        // We've stopped scanning because we timed out.
        bt_log(TRACE, "gap-bredr", "inquiry complete, restart");
        self->MaybeStartInquiry();
      },
      hci::kInquiryCompleteEventCode, {hci::kRemoteNameRequest});
}

// Stops the inquiry procedure.
void BrEdrDiscoveryManager::StopInquiry() {
  ZX_DEBUG_ASSERT(result_handler_id_);
  bt_log(TRACE, "gap-bredr", "cancelling inquiry");

  auto inq_cancel = hci::CommandPacket::New(hci::kInquiryCancel);
  hci_->command_channel()->SendCommand(std::move(inq_cancel), [](long, const auto& event) {
    // Warn if the command failed.
    hci_is_error(event, WARN, "gap-bredr", "inquiry cancel failed");
  });
}

hci::CommandChannel::EventCallbackResult BrEdrDiscoveryManager::InquiryResult(
    const hci::EventPacket& event) {
  std::unordered_set<Peer*> peers;
  if (event.event_code() == hci::kInquiryResultEventCode) {
    peers = ProcessInquiryResult<hci::InquiryResultEventParams, hci::InquiryResult>(cache_, event);
  } else if (event.event_code() == hci::kInquiryResultWithRSSIEventCode) {
    peers = ProcessInquiryResult<hci::InquiryResultWithRSSIEventParams, hci::InquiryResultRSSI>(
        cache_, event);
  } else {
    bt_log(ERROR, "gap-bredr", "unsupported inquiry result type");
    return hci::CommandChannel::EventCallbackResult::kContinue;
  }

  for (Peer* peer : peers) {
    if (!peer->name()) {
      RequestPeerName(peer->identifier());
    }
    for (const auto& session : discovering_) {
      session->NotifyDiscoveryResult(*peer);
    }
  }
  return hci::CommandChannel::EventCallbackResult::kContinue;
}

hci::CommandChannel::EventCallbackResult BrEdrDiscoveryManager::ExtendedInquiryResult(
    const hci::EventPacket& event) {
  ZX_DEBUG_ASSERT(event.event_code() == hci::kExtendedInquiryResultEventCode);

  bt_log(TRACE, "gap-bredr", "ExtendedInquiryResult received");
  if (event.view().payload_size() != sizeof(hci::ExtendedInquiryResultEventParams)) {
    bt_log(WARN, "gap-bredr", "ignoring malformed result (%zu bytes)", event.view().payload_size());
    return hci::CommandChannel::EventCallbackResult::kContinue;
  }
  const auto& result = event.params<hci::ExtendedInquiryResultEventParams>();

  DeviceAddress addr(DeviceAddress::Type::kBREDR, result.bd_addr);
  Peer* peer = cache_->FindByAddress(addr);
  if (!peer) {
    peer = cache_->NewPeer(addr, true);
  }
  ZX_DEBUG_ASSERT(peer);

  peer->MutBrEdr().SetInquiryData(result);

  if (!peer->name()) {
    RequestPeerName(peer->identifier());
  }
  for (const auto& session : discovering_) {
    session->NotifyDiscoveryResult(*peer);
  }
  return hci::CommandChannel::EventCallbackResult::kContinue;
}

void BrEdrDiscoveryManager::UpdateEIRResponseData(std::string name, hci::StatusCallback callback) {
  DataType name_type = DataType::kCompleteLocalName;
  size_t name_size = name.size();
  if (name.size() >= (hci::kExtendedInquiryResponseMaxNameBytes)) {
    name_type = DataType::kShortenedLocalName;
    name_size = hci::kExtendedInquiryResponseMaxNameBytes;
  }
  auto self = weak_ptr_factory_.GetWeakPtr();
  auto eir_packet = hci::CommandPacket::New(hci::kWriteExtendedInquiryResponse,
                                            sizeof(hci::WriteExtendedInquiryResponseParams));
  eir_packet->mutable_payload<hci::WriteExtendedInquiryResponseParams>()->fec_required = 0x00;
  auto eir_response_buf =
      MutableBufferView(eir_packet->mutable_payload<hci::WriteExtendedInquiryResponseParams>()
                            ->extended_inquiry_response,
                        hci::kExtendedInquiryResponseBytes);
  eir_response_buf.Fill(0);
  eir_response_buf[0] = name.length() + 1;
  eir_response_buf[1] = static_cast<uint8_t>(name_type);
  eir_response_buf.mutable_view(2).Write(reinterpret_cast<const uint8_t*>(name.data()), name_size);
  self->hci_->command_channel()->SendCommand(
      std::move(eir_packet), [self, name = std::move(name), cb = std::move(callback)](
                                 auto, const hci::EventPacket& event) mutable {
        if (!hci_is_error(event, WARN, "gap", "write EIR failed")) {
          self->local_name_ = std::move(name);
        }
        cb(event.ToStatus());
      });
}

void BrEdrDiscoveryManager::UpdateLocalName(std::string name, hci::StatusCallback callback) {
  size_t name_size = name.size();
  if (name.size() >= hci::kMaxNameLength) {
    name_size = hci::kMaxNameLength;
  }
  auto self = weak_ptr_factory_.GetWeakPtr();
  auto write_name =
      hci::CommandPacket::New(hci::kWriteLocalName, sizeof(hci::WriteLocalNameCommandParams));
  auto name_buf =
      MutableBufferView(write_name->mutable_payload<hci::WriteLocalNameCommandParams>()->local_name,
                        hci::kMaxNameLength);
  name_buf.Fill(0);
  name_buf.Write(reinterpret_cast<const uint8_t*>(name.data()), name_size);
  hci_->command_channel()->SendCommand(
      std::move(write_name), [self, name = std::move(name), cb = std::move(callback)](
                                 auto, const hci::EventPacket& event) mutable {
        if (hci_is_error(event, WARN, "gap", "set local name failed")) {
          cb(event.ToStatus());
          return;
        }
        // If the WriteLocalName command was successful, update the extended inquiry data.
        self->UpdateEIRResponseData(std::move(name), std::move(cb));
      });
}

void BrEdrDiscoveryManager::RequestPeerName(PeerId id) {
  if (requesting_names_.count(id)) {
    bt_log(TRACE, "gap-bredr", "already requesting name for %s", bt_str(id));
    return;
  }
  Peer* peer = cache_->FindById(id);
  if (!peer) {
    bt_log(WARN, "gap-bredr", "cannot request name, unknown id: %s", bt_str(id));
    return;
  }
  auto packet =
      hci::CommandPacket::New(hci::kRemoteNameRequest, sizeof(hci::RemoteNameRequestCommandParams));
  packet->mutable_view()->mutable_payload_data().SetToZeros();
  auto params = packet->mutable_payload<hci::RemoteNameRequestCommandParams>();
  ZX_DEBUG_ASSERT(peer->bredr());
  ZX_DEBUG_ASSERT(peer->bredr()->page_scan_repetition_mode());
  params->bd_addr = peer->address().value();
  params->page_scan_repetition_mode = *(peer->bredr()->page_scan_repetition_mode());
  if (peer->bredr()->clock_offset()) {
    params->clock_offset = htole16(*(peer->bredr()->clock_offset()));
  }

  auto cb = [id, self = weak_ptr_factory_.GetWeakPtr()](auto, const auto& event) {
    if (!self) {
      return;
    }
    if (hci_is_error(event, TRACE, "gap-bredr", "remote name request failed")) {
      self->requesting_names_.erase(id);
      return;
    }

    if (event.event_code() == hci::kCommandStatusEventCode) {
      return;
    }

    ZX_DEBUG_ASSERT(event.event_code() == hci::kRemoteNameRequestCompleteEventCode);

    self->requesting_names_.erase(id);
    const auto& params = event.view().template payload<hci::RemoteNameRequestCompleteEventParams>();
    for (size_t len = 0; len <= hci::kMaxNameLength; len++) {
      if (params.remote_name[len] == 0 || len == hci::kMaxNameLength) {
        Peer* peer = self->cache_->FindById(id);
        if (peer) {
          peer->SetName(std::string(params.remote_name, params.remote_name + len));
        }
        return;
      }
    }
  };

  auto cmd_id = hci_->command_channel()->SendExclusiveCommand(
      std::move(packet), std::move(cb), hci::kRemoteNameRequestCompleteEventCode, {hci::kInquiry});
  if (cmd_id) {
    requesting_names_.insert(id);
  }
}

void BrEdrDiscoveryManager::RequestDiscoverable(DiscoverableCallback callback) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(callback);

  bt_log(DEBUG, "gap-bredr", "RequestDiscoverable");

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto status_cb = [self, cb = callback.share()](const auto& status) {
    cb(status, (status ? self->AddDiscoverableSession() : nullptr));
  };

  if (!pending_discoverable_.empty()) {
    bt_log(DEBUG, "gap-bredr", "discoverable mode starting, add to pending");
    pending_discoverable_.push(std::move(status_cb));
    return;
  }

  // If we're already discoverable, just add a session.
  if (!discoverable_.empty()) {
    bt_log(DEBUG, "gap-bredr", "add to active discoverable");
    auto session = AddDiscoverableSession();
    callback(hci::Status(), std::move(session));
    return;
  }

  pending_discoverable_.push(std::move(status_cb));
  SetInquiryScan();
}

void BrEdrDiscoveryManager::SetInquiryScan() {
  bool enable = !discoverable_.empty() || !pending_discoverable_.empty();
  bt_log(TRACE, "gap-bredr", "%s inquiry scan", (enable ? "enabling" : "disabling"));

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto scan_enable_cb = [self](auto, const hci::EventPacket& event) {
    if (!self) {
      return;
    }

    auto status = event.ToStatus();
    auto resolve_pending = fit::defer([self, &status]() {
      while (!self->pending_discoverable_.empty()) {
        auto cb = std::move(self->pending_discoverable_.front());
        self->pending_discoverable_.pop();
        cb(status);
      }
    });

    if (bt_is_error(status, WARN, "gap-bredr", "read scan enable failed")) {
      return;
    }

    bool enable = !self->discoverable_.empty() || !self->pending_discoverable_.empty();
    auto params = event.return_params<hci::ReadScanEnableReturnParams>();
    uint8_t scan_type = params->scan_enable;
    bool enabled = scan_type & static_cast<uint8_t>(hci::ScanEnableBit::kInquiry);

    if (enable == enabled) {
      bt_log(INFO, "gap-bredr", "inquiry scan already %s", (enable ? "enabled" : "disabled"));
      return;
    }

    if (enable) {
      scan_type |= static_cast<uint8_t>(hci::ScanEnableBit::kInquiry);
    } else {
      scan_type &= ~static_cast<uint8_t>(hci::ScanEnableBit::kInquiry);
    }
    auto write_enable =
        hci::CommandPacket::New(hci::kWriteScanEnable, sizeof(hci::WriteScanEnableCommandParams));
    write_enable->mutable_payload<hci::WriteScanEnableCommandParams>()->scan_enable = scan_type;
    resolve_pending.cancel();
    self->hci_->command_channel()->SendCommand(
        std::move(write_enable), [self](auto, const hci::EventPacket& event) {
          if (!self) {
            return;
          }

          // Warn if the command failed
          hci_is_error(event, WARN, "gap-bredr", "write scan enable failed");

          while (!self->pending_discoverable_.empty()) {
            auto cb = std::move(self->pending_discoverable_.front());
            self->pending_discoverable_.pop();
            cb(event.ToStatus());
          }
        });
  };

  auto read_enable = hci::CommandPacket::New(hci::kReadScanEnable);
  hci_->command_channel()->SendCommand(std::move(read_enable), std::move(scan_enable_cb));
}

void BrEdrDiscoveryManager::WriteInquiryScanSettings(uint16_t interval, uint16_t window,
                                                     bool interlaced) {
  // TODO(jamuraa): add a callback for success or failure?
  auto write_activity = hci::CommandPacket::New(hci::kWriteInquiryScanActivity,
                                                sizeof(hci::WriteInquiryScanActivityCommandParams));
  auto* activity_params =
      write_activity->mutable_payload<hci::WriteInquiryScanActivityCommandParams>();
  activity_params->inquiry_scan_interval = htole16(interval);
  activity_params->inquiry_scan_window = htole16(window);

  hci_->command_channel()->SendCommand(
      std::move(write_activity), [](auto id, const hci::EventPacket& event) {
        if (hci_is_error(event, WARN, "gap-bredr", "write inquiry scan activity failed")) {
          return;
        }
        bt_log(TRACE, "gap-bredr", "inquiry scan activity updated");
      });

  auto write_type = hci::CommandPacket::New(hci::kWriteInquiryScanType,
                                            sizeof(hci::WriteInquiryScanTypeCommandParams));
  auto* type_params = write_type->mutable_payload<hci::WriteInquiryScanTypeCommandParams>();
  type_params->inquiry_scan_type =
      (interlaced ? hci::InquiryScanType::kInterlacedScan : hci::InquiryScanType::kStandardScan);

  hci_->command_channel()->SendCommand(
      std::move(write_type), [](auto id, const hci::EventPacket& event) {
        if (hci_is_error(event, WARN, "gap-bredr", "write inquiry scan type failed")) {
          return;
        }
        bt_log(TRACE, "gap-bredr", "inquiry scan type updated");
      });
}

std::unique_ptr<BrEdrDiscoverySession> BrEdrDiscoveryManager::AddDiscoverySession() {
  bt_log(TRACE, "gap-bredr", "adding discovery session");

  // Cannot use make_unique here since BrEdrDiscoverySession has a private
  // constructor.
  std::unique_ptr<BrEdrDiscoverySession> session(
      new BrEdrDiscoverySession(weak_ptr_factory_.GetWeakPtr()));
  ZX_DEBUG_ASSERT(discovering_.find(session.get()) == discovering_.end());
  discovering_.insert(session.get());
  return session;
}

void BrEdrDiscoveryManager::RemoveDiscoverySession(BrEdrDiscoverySession* session) {
  bt_log(TRACE, "gap-bredr", "removing discovery session");

  auto removed = discovering_.erase(session);
  // TODO(NET-619): Cancel the running inquiry with StopInquiry() instead.
  if (removed) {
    zombie_discovering_.insert(session);
  }
}

std::unique_ptr<BrEdrDiscoverableSession> BrEdrDiscoveryManager::AddDiscoverableSession() {
  bt_log(TRACE, "gap-bredr", "adding discoverable session");

  // Cannot use make_unique here since BrEdrDiscoverableSession has a private
  // constructor.
  std::unique_ptr<BrEdrDiscoverableSession> session(
      new BrEdrDiscoverableSession(weak_ptr_factory_.GetWeakPtr()));
  ZX_DEBUG_ASSERT(discoverable_.find(session.get()) == discoverable_.end());
  discoverable_.insert(session.get());
  return session;
}

void BrEdrDiscoveryManager::RemoveDiscoverableSession(BrEdrDiscoverableSession* session) {
  bt_log(TRACE, "gap-bredr", "removing discoverable session");
  discoverable_.erase(session);
  if (discoverable_.empty()) {
    SetInquiryScan();
  }
}

void BrEdrDiscoveryManager::InvalidateDiscoverySessions() {
  for (auto session : discovering_) {
    session->NotifyError();
  }
  discovering_.clear();
}

}  // namespace gap
}  // namespace bt
