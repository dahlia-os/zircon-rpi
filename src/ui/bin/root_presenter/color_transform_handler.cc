// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/color_transform_handler.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace root_presenter {

const std::array<float, 3> kZero = {0, 0, 0};

ColorTransformHandler::ColorTransformHandler(sys::ComponentContext* component_context,
                                             scenic::ResourceId compositor_id,
                                             scenic::Session* session)
    : ColorTransformHandler(component_context, compositor_id, session, ColorTransformState()) {}

ColorTransformHandler::ColorTransformHandler(sys::ComponentContext* component_context,
                                             scenic::ResourceId compositor_id,
                                             scenic::Session* session, ColorTransformState state)
    : component_context_(component_context),
      session_(session),
      compositor_id_(compositor_id),
      color_transform_handler_bindings_(this),
      color_transform_state_(state) {
  FX_DCHECK(component_context_);
  FX_DCHECK(session_);
  component_context->svc()->Connect(color_transform_manager_.NewRequest());
  color_transform_manager_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Unable to connect to ColorTransformManager" << zx_status_get_string(status);
  });
  fidl::InterfaceHandle<fuchsia::accessibility::ColorTransformHandler> handle;
  color_transform_handler_bindings_.Bind(handle.NewRequest());
  color_transform_manager_->RegisterColorTransformHandler(std::move(handle));
  component_context->outgoing()->AddPublicService(color_adjustment_bindings_.GetHandler(this));
}

ColorTransformHandler::~ColorTransformHandler() {
  component_context_->outgoing()
      ->RemovePublicService<fuchsia::ui::brightness::ColorAdjustmentHandler>();
}

void ColorTransformHandler::SetColorTransformConfiguration(
    fuchsia::accessibility::ColorTransformConfiguration configuration,
    SetColorTransformConfigurationCallback callback) {
  if (!configuration.has_color_adjustment_matrix()) {
    FX_LOGS(ERROR) << "ColorTransformConfiguration missing color adjustment matrix.";
    return;
  }
  if (!configuration.has_color_adjustment_pre_offset()) {
    FX_LOGS(ERROR) << "ColorTransformConfiguration missing color adjustment pre offset vector.";
    return;
  }
  if (!configuration.has_color_adjustment_post_offset()) {
    FX_LOGS(ERROR) << "ColorTransformConfiguration missing color adjustment post offset vector.";
    return;
  }
  SetScenicColorConversion(configuration.color_adjustment_matrix(),
                           configuration.color_adjustment_pre_offset(),
                           configuration.color_adjustment_post_offset());
  color_transform_state_.Update(std::move(configuration));
}

void ColorTransformHandler::SetColorAdjustment(
    fuchsia::ui::brightness::ColorAdjustmentTable color_adjustment_table) {
  if (color_transform_state_.IsActive()) {
    FX_LOGS(INFO) << "Ignoring SetColorAdjustment because color correction is currently active.";
    return;
  }

  if (!color_adjustment_table.has_matrix()) {
    FX_LOGS(INFO) << "Ignoring SetColorAdjustment because matrix is empty";
    return;
  }

  SetScenicColorConversion(color_adjustment_table.matrix(), /* preoffsets */ kZero,
                           /* postoffsets */ kZero);
}

void ColorTransformHandler::SetScenicColorConversion(
    const std::array<float, 9> color_transform_matrix,
    const std::array<float, 3> color_transform_pre_offsets,
    const std::array<float, 3> color_transform_post_offsets) {
  // Create scenic color adjustment cmd.
  fuchsia::ui::gfx::Command color_adjustment_cmd;
  fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK display_color_conversion_cmd;
  InitColorConversionCmd(&display_color_conversion_cmd, color_transform_matrix,
                         color_transform_pre_offsets, color_transform_post_offsets);
  // Call scenic to apply color adjustment.
  color_adjustment_cmd.set_set_display_color_conversion(std::move(display_color_conversion_cmd));
  session_->Enqueue(std::move(color_adjustment_cmd));
  session_->Present(0, [](fuchsia::images::PresentationInfo info) {});
}

void ColorTransformHandler::InitColorConversionCmd(
    fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK* display_color_conversion_cmd,
    const std::array<float, 9> color_transform_matrix,
    const std::array<float, 3> color_transform_pre_offsets,
    const std::array<float, 3> color_transform_post_offsets) {
  display_color_conversion_cmd->compositor_id = compositor_id_;
  display_color_conversion_cmd->preoffsets = color_transform_pre_offsets;
  display_color_conversion_cmd->matrix = color_transform_matrix;
  display_color_conversion_cmd->postoffsets = color_transform_post_offsets;
}

}  // namespace root_presenter
