// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/zx/event.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/semantics/semantic_tree.h"
#include "src/ui/a11y/lib/semantics/tests/semantic_tree_parser.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {
namespace {
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;
using ::testing::ElementsAre;

const int kMaxLogBufferSize = 1024;

class MockSemanticTree : public ::a11y::SemanticTree {
 public:
  bool Update(TreeUpdates updates) override {
    for (const auto& update : updates) {
      if (update.has_delete_node_id()) {
        deleted_node_ids_.push_back(update.delete_node_id());
        received_updates_.emplace_back(update.delete_node_id());
      } else if (update.has_node()) {
        Node copy1;
        Node copy2;
        update.node().Clone(&copy1);
        update.node().Clone(&copy2);
        updated_nodes_.push_back(std::move(copy1));
        received_updates_.emplace_back(std::move(copy2));
      }
    }
    if (reject_commit_) {
      return false;
    }
    return ::a11y::SemanticTree::Update(std::move(updates));
  }

  void WillReturnFalseOnNextCommit() { reject_commit_ = true; }

  void ClearMockStatus() {
    received_updates_.clear();
    deleted_node_ids_.clear();
    updated_nodes_.clear();
    reject_commit_ = false;
  }

  TreeUpdates& received_updates() { return received_updates_; }

  std::vector<uint32_t>& deleted_node_ids() { return deleted_node_ids_; }

  std::vector<Node>& updated_nodes() { return updated_nodes_; }

 private:
  // A copy of the updates sent to this tree.
  TreeUpdates received_updates_;

  std::vector<uint32_t> deleted_node_ids_;

  std::vector<Node> updated_nodes_;

  bool reject_commit_ = false;
};

const std::string kSemanticTreeSingleNodePath = "/pkg/data/semantic_tree_single_node.json";
const std::string kSemanticTreeOddNodesPath = "/pkg/data/semantic_tree_odd_nodes.json";

auto NodeIdEq(uint32_t node_id) { return testing::Property(&Node::node_id, node_id); }

class SemanticTreeServiceTest : public gtest::TestLoopFixture {
 public:
  SemanticTreeServiceTest() {}

 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();
    // Create View Ref.
    zx::eventpair a;
    zx::eventpair::create(0u, &a, &b_);

    a11y::SemanticTreeService::CloseChannelCallback close_channel_callback(
        [this](zx_status_t status) {
          this->close_channel_called_ = true;
          this->close_channel_status_ = status;
        });
    auto tree = std::make_unique<MockSemanticTree>();
    tree_ptr_ = tree.get();
    semantic_tree_ = std::make_unique<a11y::SemanticTreeService>(
        std::move(tree), koid_, fuchsia::accessibility::semantics::SemanticListenerPtr() /*unused*/,
        debug_dir(), std::move(close_channel_callback));
  }

  std::vector<Node> BuildUpdatesFromFile(const std::string& file_path) {
    std::vector<Node> nodes;
    EXPECT_TRUE(semantic_tree_parser_.ParseSemanticTree(file_path, &nodes));
    return nodes;
  }

  void InitializeTreeNodesFromFile(const std::string& file_path) {
    ::a11y::SemanticTree::TreeUpdates updates;
    std::vector<Node> nodes;
    EXPECT_TRUE(semantic_tree_parser_.ParseSemanticTree(file_path, &nodes));
    for (auto& node : nodes) {
      updates.emplace_back(std::move(node));
    }
    EXPECT_TRUE(tree_ptr_->Update(std::move(updates)));
    tree_ptr_->ClearMockStatus();
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  int OpenAsFD(vfs::internal::Node* node, async_dispatcher_t* dispatcher) {
    zx::channel local, remote;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
    EXPECT_EQ(ZX_OK, node->Serve(fuchsia::io::OPEN_RIGHT_READABLE, std::move(remote), dispatcher));
    int fd = -1;
    EXPECT_EQ(ZX_OK, fdio_fd_create(local.release(), &fd));
    return fd;
  }

  char* ReadFile(vfs::internal::Node* node, int length, char* buffer) {
    EXPECT_LE(length, kMaxLogBufferSize);
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    loop.StartThread("ReadingDebugFile");

    int fd = OpenAsFD(node, loop.dispatcher());
    EXPECT_LE(0, fd);

    memset(buffer, 0, kMaxLogBufferSize);
    EXPECT_EQ(length, pread(fd, buffer, length, 0));
    return buffer;
  }

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<a11y::SemanticTreeService> semantic_tree_;
  MockSemanticTree* tree_ptr_ = nullptr;
  bool close_channel_called_ = false;
  zx_status_t close_channel_status_ = 0;
  zx_koid_t koid_ = 12345;
  SemanticTreeParser semantic_tree_parser_;

  // The event signaling pair member, used to invalidate the View Ref.
  zx::eventpair b_;
};

TEST_F(SemanticTreeServiceTest, IsSameViewReturnsTrueForTreeViewRef) {
  EXPECT_EQ(semantic_tree_->view_ref_koid(), koid_);
}

TEST_F(SemanticTreeServiceTest, UpdatesAreSentOnlyAfterCommit) {
  auto updates = BuildUpdatesFromFile(kSemanticTreeOddNodesPath);
  semantic_tree_->UpdateSemanticNodes(std::move(updates));
  EXPECT_TRUE(tree_ptr_->received_updates().empty());
  bool commit_called = false;
  auto callback = [&commit_called]() { commit_called = true; };
  semantic_tree_->CommitUpdates(std::move(callback));
  EXPECT_TRUE(commit_called);
  EXPECT_THAT(tree_ptr_->updated_nodes(),
              ElementsAre(NodeIdEq(0), NodeIdEq(1), NodeIdEq(2), NodeIdEq(3), NodeIdEq(4),
                          NodeIdEq(5), NodeIdEq(6)));
}

TEST_F(SemanticTreeServiceTest, InvalidTreeUpdatesClosesTheChannel) {
  auto updates = BuildUpdatesFromFile(kSemanticTreeOddNodesPath);
  tree_ptr_->WillReturnFalseOnNextCommit();
  semantic_tree_->UpdateSemanticNodes(std::move(updates));
  EXPECT_TRUE(tree_ptr_->received_updates().empty());
  bool commit_called = false;
  auto callback = [&commit_called]() { commit_called = true; };
  semantic_tree_->CommitUpdates(std::move(callback));
  EXPECT_TRUE(commit_called);
  // This commit failed, check if the callback to close the channel was invoked.
  EXPECT_TRUE(close_channel_called_);
  EXPECT_EQ(close_channel_status_, ZX_ERR_INVALID_ARGS);
}

TEST_F(SemanticTreeServiceTest, DeletesAreOnlySentAfterACommit) {
  auto updates = BuildUpdatesFromFile(kSemanticTreeOddNodesPath);
  semantic_tree_->UpdateSemanticNodes(std::move(updates));
  semantic_tree_->CommitUpdates([]() {});
  tree_ptr_->ClearMockStatus();

  semantic_tree_->DeleteSemanticNodes({5, 6});
  // Update the parent.
  std::vector<Node> new_updates;
  new_updates.emplace_back(CreateTestNode(2, "updated parent"));
  *new_updates.back().mutable_child_ids() = std::vector<uint32_t>();
  semantic_tree_->UpdateSemanticNodes(std::move(new_updates));
  semantic_tree_->CommitUpdates([]() {});
  EXPECT_THAT(tree_ptr_->deleted_node_ids(), ElementsAre(5, 6));
  EXPECT_THAT(tree_ptr_->updated_nodes(), ElementsAre(NodeIdEq(2)));
}

TEST_F(SemanticTreeServiceTest, EnableSemanticsUpdatesClearsTreeOnDisable) {
  InitializeTreeNodesFromFile(kSemanticTreeSingleNodePath);

  EXPECT_EQ(semantic_tree_->Get()->Size(), 1u);

  // Disable semantic updates and verify that tree is cleared.
  semantic_tree_->EnableSemanticsUpdates(false);

  EXPECT_EQ(semantic_tree_->Get()->Size(), 0u);
}

TEST_F(SemanticTreeServiceTest, LogsSemanticTree) {
  auto updates = BuildUpdatesFromFile(kSemanticTreeOddNodesPath);
  semantic_tree_->UpdateSemanticNodes(std::move(updates));
  semantic_tree_->CommitUpdates([]() {});
  const std::string expected_semantic_tree_odd =
      "ID: 0 Label:Node-0 Location: no location Transform: no transform\n"
      "    ID: 1 Label:Node-1 Location: no location Transform: no transform\n"
      "        ID: 3 Label:Node-3 Location: no location Transform: no transform\n"
      "        ID: 4 Label:Node-4 Location: no location Transform: no transform\n"
      "    ID: 2 Label:Node-2 Location: no location Transform: no transform\n"
      "        ID: 5 Label:Node-5 Location: no location Transform: no transform\n"
      "        ID: 6 Label:Node-6 Location: no location Transform: no transform\n";

  vfs::internal::Node* node;
  EXPECT_EQ(ZX_OK, debug_dir()->Lookup(std::to_string(semantic_tree_->view_ref_koid()), &node));

  char buffer[kMaxLogBufferSize];
  ReadFile(node, expected_semantic_tree_odd.size(), buffer);

  EXPECT_EQ(expected_semantic_tree_odd, buffer);
}
}  // namespace
}  // namespace accessibility_test
