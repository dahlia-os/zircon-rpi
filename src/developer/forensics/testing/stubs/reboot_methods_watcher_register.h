// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_REBOOT_METHODS_WATCHER_REGISTER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_REBOOT_METHODS_WATCHER_REGISTER_H_

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl_test_base.h>

#include <utility>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using RebootMethodsWatcherRegisterBase = SINGLE_BINDING_STUB_FIDL_SERVER(
    fuchsia::hardware::power::statecontrol, RebootMethodsWatcherRegister);

class RebootMethodsWatcherRegister : public RebootMethodsWatcherRegisterBase {
 public:
  RebootMethodsWatcherRegister(fuchsia::hardware::power::statecontrol::RebootReason reason)
      : reason_(reason) {}

  // |fuchsia::hardware::power::statecontrol::RebootMethodsWatcherRegister|.
  void Register(
      ::fidl::InterfaceHandle<fuchsia::hardware::power::statecontrol::RebootMethodsWatcher> watcher)
      override;

 private:
  fuchsia::hardware::power::statecontrol::RebootReason reason_;
  fuchsia::hardware::power::statecontrol::RebootMethodsWatcherPtr watcher_;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_REBOOT_METHODS_WATCHER_REGISTER_H_
