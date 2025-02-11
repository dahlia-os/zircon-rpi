// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/session.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <lib/trace/event.h>

#include <memory>
#include <utility>

#include <fbl/auto_call.h>

#include "src/ui/lib/escher/hmd/pose_buffer.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/util/type_utils.h"
#include "src/ui/scenic/lib/gfx/engine/gfx_command_applier.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/swapchain/swapchain_factory.h"
#include "src/ui/scenic/lib/gfx/util/time.h"
#include "src/ui/scenic/lib/gfx/util/unwrap.h"
#include "src/ui/scenic/lib/gfx/util/wrap.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

using scheduling::Present2Info;

namespace scenic_impl {
namespace gfx {

Session::Session(SessionId id, SessionContext session_context,
                 std::shared_ptr<EventReporter> event_reporter,
                 std::shared_ptr<ErrorReporter> error_reporter, inspect::Node inspect_node)
    : id_(id),
      error_reporter_(std::move(error_reporter)),
      event_reporter_(std::move(event_reporter)),
      session_context_(std::move(session_context)),
      resource_context_(
          /* Sessions can be used in integration tests, with and without Vulkan.
             When Vulkan is unavailable, the Escher pointer is null. These
             ternaries protect against null-pointer dispatching for these
             non-Vulkan tests. */
          {session_context_.vk_device,
           session_context_.escher != nullptr ? session_context_.escher->vk_physical_device()
                                              : vk::PhysicalDevice(),
           session_context_.escher != nullptr ? session_context_.escher->device()->dispatch_loader()
                                              : vk::DispatchLoaderDynamic(),
           session_context_.escher != nullptr ? session_context_.escher->device()->caps()
                                              : escher::VulkanDeviceQueues::Caps(),
           session_context_.escher_resource_recycler, session_context_.escher_image_factory,
           session_context_.escher != nullptr ? session_context_.escher->sampler_cache()
                                              : nullptr}),
      resources_(error_reporter_),
      view_tree_updater_(id),
      inspect_node_(std::move(inspect_node)),
      weak_factory_(this) {
  FX_DCHECK(error_reporter_);
  FX_DCHECK(event_reporter_);

  inspect_resource_count_ = inspect_node_.CreateUint("resource_count", 0);
}

Session::~Session() {
  resources_.Clear();
  scheduled_updates_ = {};
  FX_CHECK(resource_count_ == 0) << "Session::~Session(): " << resource_count_
                                 << " resources have not yet been destroyed.";
}

void Session::DispatchCommand(fuchsia::ui::scenic::Command command,
                              scheduling::PresentId present_id) {
  FX_DCHECK(command.Which() == fuchsia::ui::scenic::Command::Tag::kGfx);
  FX_DCHECK(scheduled_updates_.empty() || scheduled_updates_.front().present_id <= present_id);
  scheduled_updates_.emplace(present_id, std::move(command.gfx()));
}

EventReporter* Session::event_reporter() const { return event_reporter_.get(); }

bool Session::ApplyScheduledUpdates(CommandContext* command_context,
                                    scheduling::PresentId present_id) {
  // RAII object to ensure UpdateViewHolderConnections and StageViewTreeUpdates, on all exit paths.
  fbl::AutoCall cleanup([this, command_context] {
    view_tree_updater_.UpdateViewHolderConnections();
    view_tree_updater_.StageViewTreeUpdates(command_context->scene_graph.get());
  });

  // Batch all updates up to |present_id|.
  std::vector<::fuchsia::ui::gfx::Command> commands;
  while (!scheduled_updates_.empty() && scheduled_updates_.front().present_id <= present_id) {
    std::move(scheduled_updates_.front().commands.begin(),
              scheduled_updates_.front().commands.end(), std::back_inserter(commands));
    scheduled_updates_.pop();
  }

  if (!ApplyUpdate(command_context, std::move(commands))) {
    // An error was encountered while applying the update.
    FX_LOGS(WARNING) << "scenic_impl::gfx::Session::ApplyScheduledUpdates(): "
                        "An error was encountered while applying the update. "
                        "Initiating teardown.";
    // Update failed. Do not handle any additional updates and clear any pending updates.
    scheduled_updates_ = {};
    return false;
  }

  // Updates have been applied - inspect latest session resource and tree stats.
  inspect_resource_count_.Set(resource_count_);

  return true;
}

void Session::EnqueueEvent(::fuchsia::ui::gfx::Event event) {
  event_reporter_->EnqueueEvent(std::move(event));
}

void Session::EnqueueEvent(::fuchsia::ui::input::InputEvent event) {
  event_reporter_->EnqueueEvent(std::move(event));
}

bool Session::SetRootView(fxl::WeakPtr<View> view) {
  // Check that the root view ID is being set or being cleared. If there is
  // already a root view, another cannot be set.
  if (root_view_) {
    return false;
  }

  root_view_ = view;
  return true;
}

bool Session::ApplyUpdate(CommandContext* command_context,
                          std::vector<::fuchsia::ui::gfx::Command> commands) {
  TRACE_DURATION("gfx", "Session::ApplyUpdate");
  for (auto& command : commands) {
    if (!ApplyCommand(command_context, std::move(command))) {
      error_reporter_->ERROR() << "scenic_impl::gfx::Session::ApplyCommand() "
                                  "failed to apply Command: "
                               << command;
      return false;
    }
  }
  return true;
}

}  // namespace gfx
}  // namespace scenic_impl
