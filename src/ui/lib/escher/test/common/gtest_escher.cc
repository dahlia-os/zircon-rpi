// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/common/gtest_escher.h"

#include <vulkan/vulkan.h>

#include "src/ui/lib/escher/escher_process_init.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {
namespace {

#if !ESCHER_USE_RUNTIME_GLSL
static void LoadShadersFromDisk(HackFilesystemPtr fs) {
  // NOTE: this and ../shaders/shaders.gni must be kept in sync.
  const std::vector<HackFilePath> paths = {
      // Flatland renderer.
      "shaders/shaders_flatland_flat_main_frag14695981039346656037.spirv",
      "shaders/shaders_flatland_flat_main_vert14695981039346656037.spirv",

      // Paper renderer.
      "shaders/shaders_model_renderer_main_frag12890958529260787213.spirv",
      "shaders/shaders_model_renderer_main_frag15276133142244279294.spirv",
      "shaders/shaders_model_renderer_main_frag4304586084079301274.spirv",
      "shaders/shaders_model_renderer_main_vert12890958529260787213.spirv",
      "shaders/shaders_model_renderer_main_vert15064700897732225279.spirv",
      "shaders/shaders_model_renderer_main_vert15276133142244279294.spirv",
      "shaders/shaders_model_renderer_main_vert4304586084079301274.spirv",
      "shaders/shaders_model_renderer_main_vert817427562299648481.spirv",
      "shaders/shaders_paper_frag_main_ambient_light_frag4304586084079301274.spirv",
      "shaders/shaders_paper_frag_main_point_light_frag15064700897732225279.spirv",
      "shaders/shaders_paper_frag_main_point_light_frag817427562299648481.spirv",

      // Pose buffer latching compute shader, from pose_buffer_latching_shader.cc.
      "shaders/shaders_compute_pose_buffer_latching_comp14695981039346656037.spirv",
  };
  FX_CHECK(fs->InitializeWithRealFiles(paths));
}
#endif

}  // namespace

Escher* GetEscher() {
  EXPECT_FALSE(VK_TESTS_SUPPRESSED());
  return EscherEnvironment::GetGlobalTestEnvironment()->GetEscher();
}

bool GlobalEscherUsesVirtualGpu() {
  vk::PhysicalDevice physical_device =
      EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanDevice()->vk_physical_device();
  return physical_device.getProperties().deviceType == vk::PhysicalDeviceType::eVirtualGpu;
}

bool GlobalEscherUsesSwiftShader() {
  vk::PhysicalDevice physical_device =
      EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanDevice()->vk_physical_device();
  std::string physical_device_name = physical_device.getProperties().deviceName;
  return physical_device_name.find("SwiftShader") != std::string::npos;
}

void EscherEnvironment::RegisterGlobalTestEnvironment() {
  FX_CHECK(global_escher_environment_ == nullptr);
  global_escher_environment_ = static_cast<escher::test::EscherEnvironment*>(
      testing::AddGlobalTestEnvironment(new escher::test::EscherEnvironment));
}

EscherEnvironment* EscherEnvironment::GetGlobalTestEnvironment() {
  FX_CHECK(global_escher_environment_ != nullptr);
  return global_escher_environment_;
}

void EscherEnvironment::SetUp() {
  if (!VK_TESTS_SUPPRESSED()) {
    VulkanInstance::Params instance_params(
        {{},
         {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
          VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME},
         false});
    auto validation_layer_name = VulkanInstance::GetValidationLayerName();
    if (validation_layer_name) {
      instance_params.layer_names.insert(*validation_layer_name);
    }

    VulkanDeviceQueues::Params device_params(
        {{VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME, VK_KHR_MAINTENANCE1_EXTENSION_NAME,
          VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,

          VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME},
         {},
         vk::SurfaceKHR()});
#ifdef VK_USE_PLATFORM_FUCHSIA
    device_params.required_extension_names.insert(VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME);
    device_params.required_extension_names.insert(VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
    device_params.required_extension_names.insert(VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME);
#endif
    vulkan_instance_ = VulkanInstance::New(instance_params);
    vulkan_device_ = VulkanDeviceQueues::New(vulkan_instance_, device_params);
    hack_filesystem_ = HackFilesystem::New();
#if !ESCHER_USE_RUNTIME_GLSL
    LoadShadersFromDisk(hack_filesystem_);
#endif
    escher_ = std::make_unique<Escher>(vulkan_device_, hack_filesystem_);
    FX_CHECK(escher_);

#if ESCHER_USE_RUNTIME_GLSL
    escher::GlslangInitializeProcess();
#endif
  }
}

void EscherEnvironment::TearDown() {
  if (!VK_TESTS_SUPPRESSED()) {
#if ESCHER_USE_RUNTIME_GLSL
    escher::GlslangFinalizeProcess();
#endif

    escher_.reset();
    vulkan_device_.reset();
    vulkan_instance_.reset();
  }
}

}  // namespace test
}  // namespace escher
