// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "enhanced_retransmission_mode_engines.h"

namespace bt {
namespace l2cap {
namespace internal {

// This factory function ensures that their states are properly linked and allows for unit tests of
// their linked behaviors without adding dependencies between the classes nor tie their lifetimes
// together. If tying their lifetimes together is desired, one should perhaps hold reference counts
// on the other to preserve the independence.
std::pair<std::unique_ptr<EnhancedRetransmissionModeRxEngine>,
          std::unique_ptr<EnhancedRetransmissionModeTxEngine>>
MakeLinkedEnhancedRetransmissionModeEngines(
    ChannelId channel_id, uint16_t max_tx_sdu_size, uint8_t max_transmissions,
    uint8_t n_frames_in_tx_window,
    EnhancedRetransmissionModeTxEngine::SendFrameCallback send_frame_callback,
    EnhancedRetransmissionModeTxEngine::ConnectionFailureCallback connection_failure_callback) {
  auto rx_engine =
      std::make_unique<EnhancedRetransmissionModeRxEngine>(send_frame_callback.share());
  auto tx_engine = std::make_unique<EnhancedRetransmissionModeTxEngine>(
      channel_id, max_tx_sdu_size, max_transmissions, n_frames_in_tx_window,
      send_frame_callback.share(), std::move(connection_failure_callback));

  // The direction swap here is because our acknowledgment sequence is based on the peer's
  // transmit sequence and vice versa.
  rx_engine->set_receive_seq_num_callback(
      fit::bind_member(tx_engine.get(), &EnhancedRetransmissionModeTxEngine::UpdateAckSeq));
  rx_engine->set_ack_seq_num_callback(
      fit::bind_member(tx_engine.get(), &EnhancedRetransmissionModeTxEngine::UpdateReqSeq));
  rx_engine->set_remote_busy_set_callback(
      fit::bind_member(tx_engine.get(), &EnhancedRetransmissionModeTxEngine::SetRemoteBusy));
  rx_engine->set_remote_busy_cleared_callback(
      fit::bind_member(tx_engine.get(), &EnhancedRetransmissionModeTxEngine::ClearRemoteBusy));
  rx_engine->set_single_retransmit_set_callback(
      fit::bind_member(tx_engine.get(), &EnhancedRetransmissionModeTxEngine::SetSingleRetransmit));
  rx_engine->set_range_retransmit_set_callback(
      fit::bind_member(tx_engine.get(), &EnhancedRetransmissionModeTxEngine::SetRangeRetransmit));
  return {std::move(rx_engine), std::move(tx_engine)};
}

}  // namespace internal
}  // namespace l2cap
}  // namespace bt
