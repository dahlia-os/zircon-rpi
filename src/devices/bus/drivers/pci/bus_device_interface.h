
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include <stdint.h>
#include <zircon/errors.h>

#include <fbl/ref_ptr.h>

namespace pci {

// Forward declaration to avoid device.h
class Device;
// This interface allows for bridges/devices to communicate with the top level
// Bus object to add and remove themselves from the device list of their
// particular bus instance and make MSI allocations without exposing the rest of
// the bus's interface to them or using static methods. This becomes more
// important as multiple bus instances with differing segment groups become a
// reality.
class BusDeviceInterface {
 public:
  virtual ~BusDeviceInterface() {}
  virtual zx_status_t AllocateMsi(uint32_t count, zx::msi* msi) = 0;
  virtual void LinkDevice(fbl::RefPtr<pci::Device> device) = 0;
  virtual void UnlinkDevice(pci::Device* device) = 0;
};

}  // namespace pci
