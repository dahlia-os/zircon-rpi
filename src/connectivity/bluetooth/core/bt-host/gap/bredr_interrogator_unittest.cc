// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/bluetooth/core/bt-host/gap/bredr_interrogator.h"

#include <lib/async/default.h>

#include "src/connectivity/bluetooth/core/bt-host/common/status.h"
#include "src/connectivity/bluetooth/core/bt-host/common/test_helpers.h"
#include "src/connectivity/bluetooth/core/bt-host/data/fake_domain.h"
#include "src/connectivity/bluetooth/core/bt-host/gap/peer_cache.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/hci.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/status.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/util.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/l2cap.h"
#include "src/connectivity/bluetooth/core/bt-host/testing/fake_controller_test.h"
#include "src/connectivity/bluetooth/core/bt-host/testing/fake_peer.h"
#include "src/connectivity/bluetooth/core/bt-host/testing/test_controller.h"
#include "src/connectivity/bluetooth/core/bt-host/testing/test_packets.h"

namespace bt::gap {

constexpr hci::ConnectionHandle kConnectionHandle = 0x0BAA;
const DeviceAddress kTestDevAddr(DeviceAddress::Type::kBREDR, {1});

const auto kRemoteNameRequestRsp =
    testing::CommandStatusPacket(hci::kRemoteNameRequest, hci::StatusCode::kSuccess);

const auto kReadRemoteVersionInfoRsp =
    testing::CommandStatusPacket(hci::kReadRemoteVersionInfo, hci::StatusCode::kSuccess);

const auto kReadRemoteSupportedFeaturesRsp =
    testing::CommandStatusPacket(hci::kReadRemoteSupportedFeatures, hci::StatusCode::kSuccess);

const auto kReadRemoteExtendedFeaturesRsp =
    testing::CommandStatusPacket(hci::kReadRemoteExtendedFeatures, hci::StatusCode::kSuccess);

using bt::testing::CommandTransaction;

using TestingBase = bt::testing::FakeControllerTest<bt::testing::TestController>;

class BrEdrInterrogatorTest : public TestingBase {
 public:
  BrEdrInterrogatorTest() = default;
  ~BrEdrInterrogatorTest() override = default;

  void SetUp() override {
    TestingBase::SetUp();

    peer_cache_ =
        std::make_unique<PeerCache>(inspector_.GetRoot().CreateChild(PeerCache::kInspectNodeName));
    interrogator_ = std::make_unique<BrEdrInterrogator>(peer_cache_.get(), transport()->WeakPtr(),
                                                        async_get_default_dispatcher());

    StartTestDevice();
  }

  void TearDown() override {
    RunLoopUntilIdle();
    test_device()->Stop();
    interrogator_ = nullptr;
    peer_cache_ = nullptr;
    TestingBase::TearDown();
  }

 protected:
  void QueueSuccessfulInterrogation(DeviceAddress addr, hci::ConnectionHandle conn) const {
    const DynamicByteBuffer remote_name_complete_packet =
        testing::RemoteNameRequestCompletePacket(addr);
    const DynamicByteBuffer remote_version_complete_packet =
        testing::ReadRemoteVersionInfoCompletePacket(conn);
    const DynamicByteBuffer remote_supported_complete_packet =
        testing::ReadRemoteSupportedFeaturesCompletePacket(conn, true);

    test_device()->QueueCommandTransaction(
        CommandTransaction(testing::RemoteNameRequestPacket(addr),
                           {&kRemoteNameRequestRsp, &remote_name_complete_packet}));
    test_device()->QueueCommandTransaction(
        CommandTransaction(testing::ReadRemoteVersionInfoPacket(conn),
                           {&kReadRemoteVersionInfoRsp, &remote_version_complete_packet}));
    test_device()->QueueCommandTransaction(
        CommandTransaction(testing::ReadRemoteSupportedFeaturesPacket(conn),
                           {&kReadRemoteSupportedFeaturesRsp, &remote_supported_complete_packet}));
    QueueSuccessfulReadRemoteExtendedFeatures(conn);
  }

  void QueueSuccessfulReadRemoteExtendedFeatures(hci::ConnectionHandle conn) const {
    const DynamicByteBuffer remote_extended1_complete_packet =
        testing::ReadRemoteExtended1CompletePacket(conn);
    const DynamicByteBuffer remote_extended2_complete_packet =
        testing::ReadRemoteExtended2CompletePacket(conn);

    test_device()->QueueCommandTransaction(
        CommandTransaction(testing::ReadRemoteExtended1Packet(conn),
                           {&kReadRemoteExtendedFeaturesRsp, &remote_extended1_complete_packet}));
    test_device()->QueueCommandTransaction(
        CommandTransaction(testing::ReadRemoteExtended2Packet(conn),
                           {&kReadRemoteExtendedFeaturesRsp, &remote_extended2_complete_packet}));
  }

  PeerCache* peer_cache() const { return peer_cache_.get(); }

  BrEdrInterrogator* interrogator() const { return interrogator_.get(); }

 private:
  inspect::Inspector inspector_;
  std::unique_ptr<PeerCache> peer_cache_;
  std::unique_ptr<BrEdrInterrogator> interrogator_;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BrEdrInterrogatorTest);
};

using GAP_BrEdrInterrogatorTest = BrEdrInterrogatorTest;

TEST_F(GAP_BrEdrInterrogatorTest, SuccessfulInterrogation) {
  QueueSuccessfulInterrogation(kTestDevAddr, kConnectionHandle);

  auto* peer = peer_cache()->NewPeer(kTestDevAddr, true);
  EXPECT_FALSE(peer->name());
  EXPECT_FALSE(peer->version());
  EXPECT_FALSE(peer->features().HasPage(0));
  EXPECT_FALSE(peer->features().HasBit(0, hci::LMPFeature::kExtendedFeatures));
  EXPECT_EQ(0u, peer->features().last_page_number());

  std::optional<hci::Status> status;
  interrogator()->Start(peer->identifier(), kConnectionHandle,
                        [&status](hci::Status cb_status) { status = cb_status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(status->is_success());

  EXPECT_TRUE(peer->name());
  EXPECT_TRUE(peer->version());
  EXPECT_TRUE(peer->features().HasPage(0));
  EXPECT_TRUE(peer->features().HasBit(0, hci::LMPFeature::kExtendedFeatures));
  EXPECT_EQ(2u, peer->features().last_page_number());
}

TEST_F(GAP_BrEdrInterrogatorTest, SuccessfulReinterrogation) {
  QueueSuccessfulInterrogation(kTestDevAddr, kConnectionHandle);

  auto* peer = peer_cache()->NewPeer(kTestDevAddr, true);

  std::optional<hci::Status> status;
  interrogator()->Start(peer->identifier(), kConnectionHandle,
                        [&status](hci::Status cb_status) { status = cb_status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(status->is_success());
  status = std::nullopt;

  QueueSuccessfulReadRemoteExtendedFeatures(kConnectionHandle);
  interrogator()->Start(peer->identifier(), kConnectionHandle,
                        [&status](hci::Status cb_status) { status = cb_status; });
  RunLoopUntilIdle();
  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(status->is_success());
}

TEST_F(GAP_BrEdrInterrogatorTest, InterrogationFailedToGetName) {
  const DynamicByteBuffer remote_name_request_failure_rsp =
      testing::CommandStatusPacket(hci::kRemoteNameRequest, hci::StatusCode::kUnspecifiedError);
  test_device()->QueueCommandTransaction(CommandTransaction(
      testing::RemoteNameRequestPacket(kTestDevAddr), {&remote_name_request_failure_rsp}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(testing::ReadRemoteVersionInfoPacket(kConnectionHandle), {}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(testing::ReadRemoteSupportedFeaturesPacket(kConnectionHandle), {}));

  auto* peer = peer_cache()->NewPeer(kTestDevAddr, true);
  EXPECT_FALSE(peer->name());

  std::optional<hci::Status> status;
  interrogator()->Start(peer->identifier(), kConnectionHandle,
                        [&status](hci::Status cb_status) { status = cb_status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(status.has_value());
  EXPECT_FALSE(status->is_success());
}
}  // namespace bt::gap
