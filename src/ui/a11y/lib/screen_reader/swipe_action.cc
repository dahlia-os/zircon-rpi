// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/swipe_action.h"

#include <lib/fit/bridge.h>
#include <lib/fit/scope.h>
#include <lib/syslog/cpp/macros.h>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

namespace a11y {
SwipeAction::SwipeAction(ActionContext* action_context, ScreenReaderContext* screen_reader_context,
                         SwipeActionType action_type)
    : ScreenReaderAction(action_context, screen_reader_context), action_type_(action_type) {}

SwipeAction::~SwipeAction() = default;

void SwipeAction::Run(ActionData process_data) {
  auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
  if (!a11y_focus) {
    FX_LOGS(INFO) << "Swipe Action: No view is in focus.";
    return;
  }

  FX_DCHECK(action_context_->semantics_source);

  // Get the new node base on ActionType.
  const fuchsia::accessibility::semantics::Node* new_node;
  switch (action_type_) {
    case kNextAction:
      new_node = action_context_->semantics_source->GetNextNode(a11y_focus->view_ref_koid,
                                                                a11y_focus->node_id);
      break;
    case kPreviousAction:
      new_node = action_context_->semantics_source->GetPreviousNode(a11y_focus->view_ref_koid,
                                                                    a11y_focus->node_id);
      break;
    default:
      new_node = nullptr;
      break;
  }

  if (!new_node || !new_node->has_node_id()) {
    return;
  }

  uint32_t new_node_id = new_node->node_id();
  auto promise =
      ExecuteAccessibilityActionPromise(a11y_focus->view_ref_koid, new_node_id,
                                        fuchsia::accessibility::semantics::Action::SHOW_ON_SCREEN)
          .and_then([this, new_node_id, a11y_focus]() mutable {
            return SetA11yFocusPromise(new_node_id, a11y_focus->view_ref_koid);
          })
          .and_then([this]() { return CancelTts(); })
          .and_then([this, a11y_focus, new_node_id]() mutable {
            return BuildUtteranceFromNodePromise(a11y_focus->view_ref_koid, new_node_id);
          })
          .and_then([this](fuchsia::accessibility::tts::Utterance& utterance) mutable {
            return EnqueueUtterancePromise(std::move(utterance));
          })
          .and_then([this]() {
            // Speaks the enqueued utterance. No need to chain another promise, as this
            // is the last step.
            action_context_->tts_engine_ptr->Speak(
                [](fuchsia::accessibility::tts::Engine_Speak_Result result) {
                  if (result.is_err()) {
                    FX_LOGS(ERROR) << "Error returned while calling tts::Speak()";
                  }
                });
          })
          // Cancel any promises if this class goes out of scope.
          .wrap_with(scope_);
  auto* executor = screen_reader_context_->executor();
  executor->schedule_task(std::move(promise));
}

}  // namespace a11y
