// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantic_tree.h"

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdint>
#include <queue>
#include <string>
#include <unordered_set>

#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace a11y {
namespace {

using SemanticTreeData = std::unordered_map<uint32_t, fuchsia::accessibility::semantics::Node>;
using fuchsia::accessibility::semantics::Node;

// Tries to find |node_id| in |updated_nodes|, if not in |default_nodes|. If
// |node_id| is not present in either, returns nullptr. Please note that if
// |node_id| is present in |updated_nodes|, but the optional holds an empty
// value, this indicates a deletion and nullptr will be returned.
const Node* GetUpdatedOrDefaultNode(
    const uint32_t node_id, const std::unordered_map<uint32_t, std::optional<Node>>& updated_nodes,
    const SemanticTreeData& default_nodes) {
  if (auto it = updated_nodes.find(node_id); it != updated_nodes.end()) {
    if (it->second) {
      return &(*it->second);
    } else {
      return nullptr;
    }
  }
  if (auto it = default_nodes.find(node_id); it != default_nodes.end()) {
    return &it->second;
  }
  return nullptr;
}

// Returns a node which is a merge between |old| and |new|, where for each field
// chooses |new| if it has it, |old| otherwise.
Node MergeNodes(const Node& old_node, Node new_node) {
  Node output;
  old_node.Clone(&output);
  if (new_node.has_role()) {
    output.set_role(new_node.role());
  }

  if (new_node.has_states()) {
    output.set_states(std::move(*new_node.mutable_states()));
  }

  if (new_node.has_attributes()) {
    output.set_attributes(std::move(*new_node.mutable_attributes()));
  }

  if (new_node.has_actions()) {
    output.set_actions(new_node.actions());
  }

  if (new_node.has_child_ids()) {
    output.set_child_ids(new_node.child_ids());
  }

  if (new_node.has_location()) {
    output.set_location(new_node.location());
  }

  if (new_node.has_transform()) {
    output.set_transform(new_node.transform());
  }

  return output;
}

// Returns true if the subtree in |nodes| resulting from an update in
// |nodes_to_be_updated|, reachable from |node_id| is acyclic and that every
// child node referenced by a parent exist. |visited_nodes| is filled with the
// node ids of this traversal.
bool ValidateSubTreeForUpdate(
    const uint32_t node_id, const SemanticTreeData& nodes,
    const std::unordered_map<uint32_t, std::optional<Node>>& nodes_to_be_updated,
    std::unordered_set<uint32_t>* visited_nodes) {
  const Node* node = GetUpdatedOrDefaultNode(node_id, nodes_to_be_updated, nodes);
  if (!node) {
    // A parent node is trying to access a node that is neither in the original tree nor in the
    // updates.
    FX_LOGS(ERROR) << "Tried to visit Node [" << node_id << "] which does not exist or was deleted";
    return false;
  }
  if (auto it = visited_nodes->insert(node_id); !it.second) {
    // This node id has been already visited, which indicates a cycle in this tree.
    FX_LOGS(ERROR) << "Tried to visit already visited Node [" << node_id << "], possible cycle";
    return false;
  }
  if (node->has_child_ids()) {
    for (const auto& child_id : node->child_ids()) {
      if (!ValidateSubTreeForUpdate(child_id, nodes, nodes_to_be_updated, visited_nodes)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

SemanticTree::TreeUpdate::TreeUpdate(uint32_t delete_node_id) : delete_node_id_(delete_node_id) {}
SemanticTree::TreeUpdate::TreeUpdate(Node node) : node_(std::move(node)) {}

bool SemanticTree::TreeUpdate::has_delete_node_id() const {
  return (delete_node_id_ ? true : false);
}
bool SemanticTree::TreeUpdate::has_node() const { return (node_ ? true : false); }

uint32_t SemanticTree::TreeUpdate::TakeDeleteNodeId() {
  FX_DCHECK(has_delete_node_id());
  return std::move(*delete_node_id_);
}
Node SemanticTree::TreeUpdate::TakeNode() {
  FX_DCHECK(has_node());
  return std::move(*node_);
}

const uint32_t& SemanticTree::TreeUpdate::delete_node_id() const {
  FX_DCHECK(has_delete_node_id());
  return *delete_node_id_;
}
const Node& SemanticTree::TreeUpdate::node() const {
  FX_DCHECK(has_node());
  return *node_;
}

std::string SemanticTree::TreeUpdate::ToString() const {
  std::string output = "Update: ";

  if (has_delete_node_id()) {
    output.append("Delete Node: [" + std::to_string(delete_node_id()) + "] ");
  }
  if (has_node()) {
    std::stringstream update_node_string;

    update_node_string << "Update Node [" << std::to_string(node().node_id()) << "] Children: [";

    for (const auto child_id : node().child_ids()) {
      update_node_string << child_id << ", ";
    }
    update_node_string << ']';

    output.append(update_node_string.str());
  }
  return output;
}

SemanticTree::SemanticTree() = default;

const Node* SemanticTree::GetNode(const uint32_t node_id) const {
  const auto it = nodes_.find(node_id);
  if (it == nodes_.end()) {
    return nullptr;
  }
  return &it->second;
}

const Node* SemanticTree::GetNextNode(const uint32_t node_id) const {
  if (nodes_.find(node_id) == nodes_.end()) {
    return nullptr;
  }

  std::queue<uint32_t> nodes_to_visit;

  bool found_node = false;

  // Start traversal from the root node.
  nodes_to_visit.push(kRootNodeId);

  while (!nodes_to_visit.empty()) {
    auto current_node_id = nodes_to_visit.front();
    nodes_to_visit.pop();

    FX_DCHECK(nodes_.find(current_node_id) != nodes_.end())
        << "Nonexistent node id " << current_node_id << " encountered in tree traversal.";

    auto current_node = GetNode(current_node_id);

    if (current_node->has_child_ids()) {
      for (const auto child_id : current_node->child_ids()) {
        nodes_to_visit.push(child_id);
      }
    } else if (found_node) {
      return current_node;
    }

    if (current_node_id == node_id) {
      found_node = true;
    }
  }

  return nullptr;
}

const Node* SemanticTree::GetPreviousNode(const uint32_t node_id) const {
  if (nodes_.find(node_id) == nodes_.end()) {
    return nullptr;
  }

  std::queue<uint32_t> nodes_to_visit;

  // Start traversal from the root node.
  nodes_to_visit.push(kRootNodeId);

  const Node* previous_leaf_node = nullptr;

  while (!nodes_to_visit.empty()) {
    auto current_node_id = nodes_to_visit.front();
    nodes_to_visit.pop();

    if (current_node_id == node_id) {
      return previous_leaf_node;
    }

    FX_DCHECK(nodes_.find(current_node_id) != nodes_.end())
        << "Nonexistent node id " << current_node_id << " encountered in tree traversal.";

    auto current_node = GetNode(current_node_id);

    // Since we only want to return leaf nodes, only update previous_leaf_node if current_node does
    // not have any children.
    if (!current_node->has_child_ids()) {
      previous_leaf_node = current_node;
      continue;
    }

    for (const auto child_id : current_node->child_ids()) {
      nodes_to_visit.push(child_id);
    }
  }

  return nullptr;
}

const Node* SemanticTree::GetParentNode(const uint32_t node_id) const {
  for (const auto& kv : nodes_) {
    const auto& [unused_id, node] = kv;
    if (node.has_child_ids()) {
      const auto& child_ids = node.child_ids();
      const auto it = std::find(child_ids.begin(), child_ids.end(), node_id);
      if (it != child_ids.end()) {
        return &node;
      }
    }
  }
  return nullptr;
}

bool SemanticTree::Update(TreeUpdates updates) {
  nodes_to_be_updated_.clear();  // Prepares for a new update.
  if (updates.empty()) {
    return true;
  }
  for (auto& update : updates) {
    if (update.has_delete_node_id()) {
      nodes_to_be_updated_[update.TakeDeleteNodeId()].reset();
    } else if (update.has_node()) {
      MarkNodeForUpdate(update.TakeNode());
    }
  }

  std::unordered_set<uint32_t> visited_nodes;
  if (!ValidateUpdate(&visited_nodes)) {
    return false;
  }
  ApplyNodeUpdates(visited_nodes);
  return true;
}

bool SemanticTree::ValidateUpdate(std::unordered_set<uint32_t>* visited_nodes) {
  const Node* root = GetUpdatedOrDefaultNode(kRootNodeId, nodes_to_be_updated_, nodes_);
  if (!root) {
    // Note that there are only two occasions where the root could be null:
    // 1. The tree is empty and this is a new update to the tree without a root
    // (invalid).
    // 2. This is an update that explicitly deletes the root node (valid). This
    // effectively causes the tree to be garbage collected and all nodes are
    // deleted.
    if (auto it = nodes_to_be_updated_.find(kRootNodeId); it != nodes_to_be_updated_.end()) {
      return true;
    } else {
      return false;
    }
  }
  if (!ValidateSubTreeForUpdate(kRootNodeId, nodes_, nodes_to_be_updated_, visited_nodes)) {
    return false;
  }
  return true;
}

void SemanticTree::MarkNodeForUpdate(Node node) {
  const uint32_t node_id = node.node_id();
  if (const Node* old = GetUpdatedOrDefaultNode(node_id, nodes_to_be_updated_, nodes_);
      old == nullptr) {
    // New node. Simply mark it for future update.
    nodes_to_be_updated_[node_id] = std::move(node);
  } else {
    // Partial update.
    nodes_to_be_updated_[node_id] = MergeNodes(*old, std::move(node));
  }
}

void SemanticTree::ApplyNodeUpdates(const std::unordered_set<uint32_t>& visited_nodes) {
  // First, apply all pending updates and then delete dangling subtrees.
  for (auto& kv : nodes_to_be_updated_) {
    auto& [node_id, updated_node] = kv;
    if (updated_node) {
      nodes_[node_id] = std::move(*updated_node);
    } else {
      // The optional holds an empty value, indicating a deletion.
      nodes_.erase(node_id);
    }
  }

  // Delete dangling subtrees.
  auto it = nodes_.begin();
  while (it != nodes_.end()) {
    if (auto visited_it = visited_nodes.find(it->first); visited_it == visited_nodes.end()) {
      // node unreachable. remove from the tree.
      it = nodes_.erase(it);
    } else {
      ++it;
    }
  }
}

void SemanticTree::Clear() { nodes_.clear(); }

void SemanticTree::PerformAccessibilityAction(
    uint32_t node_id, fuchsia::accessibility::semantics::Action action,
    fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
        callback) const {
  action_handler_(node_id, action, std::move(callback));
}

void SemanticTree::PerformHitTesting(
    fuchsia::math::PointF local_point,
    fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) const {
  hit_testing_handler_(local_point, std::move(callback));
}

std::string vec3ToString(const fuchsia::ui::gfx::vec3 vec) {
  return fxl::StringPrintf("(x: %.1f, y: %.1f, z: %.1f)", vec.x, vec.y, vec.z);
}

std::string mat4ToString(const fuchsia::ui::gfx::mat4 mat) {
  std::string retval = "{ ";
  for (int i = 0; i < 4; i++) {
    retval.append(fxl::StringPrintf("col%d: (%.1f,%.1f,%.1f,%.1f), ", i, mat.matrix[i * 4],
                                    mat.matrix[i * 4 + 1], mat.matrix[i * 4 + 2],
                                    mat.matrix[i * 4 + 3]));
  }
  return retval.append(" }");
}

std::string locationToString(const fuchsia::ui::gfx::BoundingBox location) {
  return fxl::Concatenate(
      {"{ min: ", vec3ToString(location.min), ", max: ", vec3ToString(location.max), " }"});
}

std::string SemanticTree::ToString() const {
  std::function<void(const Node*, int, std::string*)> printNode;

  printNode = [&printNode, this](const Node* node, int current_level, std::string* output) {
    if (!node) {
      return;
    }

    // Add indentation
    output->append(4 * current_level, ' ');

    *output = fxl::Concatenate(
        {*output, "ID: ", std::to_string(node->node_id()), " Label:",
         node->has_attributes() && node->attributes().has_label() ? node->attributes().label()
                                                                  : "no label",
         " Location: ", node->has_location() ? locationToString(node->location()) : "no location",
         " Transform: ", node->has_transform() ? mat4ToString(node->transform()) : "no transform",
         "\n"});

    if (!node->has_child_ids()) {
      return;
    }
    for (const auto& child_id : node->child_ids()) {
      const auto* child = GetNode(child_id);
      FX_DCHECK(child);
      printNode(child, current_level + 1, output);
    }
  };

  const auto* root = GetNode(kRootNodeId);
  std::string tree_string;

  if (!root) {
    tree_string = "Root Node not found.";
    return tree_string;
  }

  printNode(root, kRootNodeId, &tree_string);

  return tree_string;
}

}  // namespace a11y
