// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/zx/channel.h>

#include "magma_util/dlog.h"
#include "platform_connection_client.h"
#include "platform_device_client.h"

namespace magma {
class ZirconPlatformDeviceClient : public PlatformDeviceClient {
 public:
  ZirconPlatformDeviceClient(magma_handle_t handle) : channel_(handle) {}

  std::unique_ptr<PlatformConnectionClient> Connect() {
    uint64_t inflight_params = 0;

    {
      uint64_t result;
      if (Query(MAGMA_QUERY_VENDOR_ID, &result)) {
        // TODO(fxb/12989) - enable for all platforms
        if (result == 0x13B5) {
          // Skipping ARM/Mali for now
        } else if (!Query(MAGMA_QUERY_MAXIMUM_INFLIGHT_PARAMS, &inflight_params)) {
          return DRETP(nullptr, "Query(MAGMA_QUERY_MAXIMUM_INFLIGHT_PARAMS) failed");
        }
      } else {
        DLOG("Query(MAGMA_QUERY_VENDOR_ID) failed");
      }
    }

    uint32_t device_handle;
    uint32_t device_notification_handle;
    zx_status_t status =
        fuchsia_gpu_magma_DeviceConnect(channel_.get(), magma::PlatformThreadId().id(),
                                        &device_handle, &device_notification_handle);
    if (status != ZX_OK)
      return DRETP(nullptr, "magma_DeviceConnect failed: %d", status);

    uint64_t max_inflight_messages = magma::upper_32_bits(inflight_params);
    uint64_t max_inflight_bytes = magma::lower_32_bits(inflight_params) * 1024 * 1024;

    return magma::PlatformConnectionClient::Create(device_handle, device_notification_handle,
                                                   max_inflight_messages, max_inflight_bytes);
  }

  bool Query(uint64_t query_id, uint64_t* result_out) {
    zx_status_t status = fuchsia_gpu_magma_DeviceQuery(channel_.get(), query_id, result_out);

    if (status != ZX_OK)
      return DRETF(false, "magma_DeviceQuery failed: %d", status);

    return true;
  }

  bool QueryReturnsBuffer(uint64_t query_id, magma_handle_t* buffer_out) {
    *buffer_out = ZX_HANDLE_INVALID;
    zx_status_t status =
        fuchsia_gpu_magma_DeviceQueryReturnsBuffer(channel_.get(), query_id, buffer_out);
    if (status != ZX_OK)
      return DRETF(false, "magma_DeviceQueryReturnsBuffer failed: %d", status);

    return true;
  }

 private:
  zx::channel channel_;
};

// static
std::unique_ptr<PlatformDeviceClient> PlatformDeviceClient::Create(uint32_t handle) {
  return std::make_unique<ZirconPlatformDeviceClient>(handle);
}
}  // namespace magma
