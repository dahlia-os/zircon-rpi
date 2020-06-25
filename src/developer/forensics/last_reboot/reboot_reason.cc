// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/reboot_reason.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace last_reboot {
namespace {

std::string ToString(const RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::kNotParseable:
      return "RebootReason::kNotParseable";
    case RebootReason::kGenericGraceful:
      return "RebootReason::kGenericGraceful";
    case RebootReason::kCold:
      return "RebootReason::kCold";
    case RebootReason::kSpontaneous:
      return "RebootReason::kSpontaneous";
    case RebootReason::kKernelPanic:
      return "RebootReason::kKernelPanic";
    case RebootReason::kOOM:
      return "RebootReason::kOOM";
    case RebootReason::kHardwareWatchdogTimeout:
      return "RebootReason::kHardwareWatchdogTimeout";
    case RebootReason::kSoftwareWatchdogTimeout:
      return "RebootReason::kSoftwareWatchdogTimeout";
    case RebootReason::kBrownout:
      return "RebootReason::kBrownout";
    case RebootReason::kUserRequest:
      return "RebootReason::kUserRequest";
    case RebootReason::kSystemUpdate:
      return "RebootReason::kSystemUpdate";
    case RebootReason::kHighTemperature:
      return "RebootReason::kHighTemperature";
    case RebootReason::kSessionFailure:
      return "RebootReason::kSessionFailure";
  }
}

}  // namespace

std::optional<bool> OptionallyGraceful(const RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::kGenericGraceful:
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kHighTemperature:
    case RebootReason::kSessionFailure:
      return true;
    case RebootReason::kCold:
    case RebootReason::kSpontaneous:
    case RebootReason::kKernelPanic:
    case RebootReason::kOOM:
    case RebootReason::kHardwareWatchdogTimeout:
    case RebootReason::kSoftwareWatchdogTimeout:
    case RebootReason::kBrownout:
      return false;
    case RebootReason::kNotParseable:
      return std::nullopt;
  }
}

cobalt::LegacyRebootReason ToCobaltLegacyRebootReason(const RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::kNotParseable:
      return cobalt::LegacyRebootReason::kKernelPanic;
    case RebootReason::kGenericGraceful:
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kHighTemperature:
    case RebootReason::kSessionFailure:
      return cobalt::LegacyRebootReason::kClean;
    case RebootReason::kCold:
      return cobalt::LegacyRebootReason::kCold;
    case RebootReason::kSpontaneous:
      return cobalt::LegacyRebootReason::kUnknown;
    case RebootReason::kKernelPanic:
      return cobalt::LegacyRebootReason::kKernelPanic;
    case RebootReason::kOOM:
      return cobalt::LegacyRebootReason::kOOM;
    case RebootReason::kHardwareWatchdogTimeout:
      return cobalt::LegacyRebootReason::kHardwareWatchdog;
    case RebootReason::kSoftwareWatchdogTimeout:
      return cobalt::LegacyRebootReason::kSoftwareWatchdog;
    case RebootReason::kBrownout:
      return cobalt::LegacyRebootReason::kBrownout;
  }
}

cobalt::LastRebootReason ToCobaltLastRebootReason(RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::kNotParseable:
      return cobalt::LastRebootReason::kUnknown;
    case RebootReason::kGenericGraceful:
      return cobalt::LastRebootReason::kGenericGraceful;
    case RebootReason::kUserRequest:
      return cobalt::LastRebootReason::kUserRequest;
    case RebootReason::kSystemUpdate:
      return cobalt::LastRebootReason::kSystemUpdate;
    case RebootReason::kHighTemperature:
      return cobalt::LastRebootReason::kHighTemperature;
    case RebootReason::kSessionFailure:
      return cobalt::LastRebootReason::kSessionFailure;
    case RebootReason::kCold:
      return cobalt::LastRebootReason::kCold;
    case RebootReason::kSpontaneous:
      return cobalt::LastRebootReason::kBriefPowerLoss;
    case RebootReason::kKernelPanic:
      return cobalt::LastRebootReason::kKernelPanic;
    case RebootReason::kOOM:
      return cobalt::LastRebootReason::kSystemOutOfMemory;
    case RebootReason::kHardwareWatchdogTimeout:
      return cobalt::LastRebootReason::kHardwareWatchdogTimeout;
    case RebootReason::kSoftwareWatchdogTimeout:
      return cobalt::LastRebootReason::kSoftwareWatchdogTimeout;
    case RebootReason::kBrownout:
      return cobalt::LastRebootReason::kBrownout;
  }
}

std::string ToCrashSignature(const RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::kNotParseable:
      return "fuchsia-reboot-log-not-parseable";
    case RebootReason::kSpontaneous:
      return "fuchsia-brief-power-loss";
    case RebootReason::kKernelPanic:
      return "fuchsia-kernel-panic";
    case RebootReason::kOOM:
      return "fuchsia-oom";
    case RebootReason::kHardwareWatchdogTimeout:
      return "fuchsia-hw-watchdog-timeout";
    case RebootReason::kSoftwareWatchdogTimeout:
      return "fuchsia-sw-watchdog-timeout";
    case RebootReason::kBrownout:
      return "fuchsia-brownout";
    case RebootReason::kGenericGraceful:
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kHighTemperature:
    case RebootReason::kSessionFailure:
    case RebootReason::kCold:
      FX_LOGS(FATAL) << "Not expecting a crash for reboot reason " << ToString(reboot_reason);
      return "FATAL ERROR";
  }
}

std::string ToCrashProgramName(const RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::kNotParseable:
      return "reboot-log";
    case RebootReason::kKernelPanic:
      return "kernel";
    case RebootReason::kBrownout:
    case RebootReason::kHardwareWatchdogTimeout:
    case RebootReason::kSpontaneous:
      return "device";
    case RebootReason::kOOM:
    case RebootReason::kSoftwareWatchdogTimeout:
      return "system";
    case RebootReason::kGenericGraceful:
    case RebootReason::kUserRequest:
    case RebootReason::kSystemUpdate:
    case RebootReason::kHighTemperature:
    case RebootReason::kSessionFailure:
    case RebootReason::kCold:
      FX_LOGS(FATAL) << "Not expecting a program name request for reboot reason "
                     << ToString(reboot_reason);
      return "FATAL ERROR";
  }
}

std::optional<fuchsia::feedback::RebootReason> ToFidlRebootReason(
    const RebootReason reboot_reason) {
  switch (reboot_reason) {
    case RebootReason::kGenericGraceful:
      return std::nullopt;
    case RebootReason::kUserRequest:
      return fuchsia::feedback::RebootReason::USER_REQUEST;
    case RebootReason::kSystemUpdate:
      return fuchsia::feedback::RebootReason::SYSTEM_UPDATE;
    case RebootReason::kHighTemperature:
      return fuchsia::feedback::RebootReason::HIGH_TEMPERATURE;
    case RebootReason::kSessionFailure:
      return fuchsia::feedback::RebootReason::SESSION_FAILURE;
    case RebootReason::kCold:
      return fuchsia::feedback::RebootReason::COLD;
    case RebootReason::kSpontaneous:
      return fuchsia::feedback::RebootReason::BRIEF_POWER_LOSS;
    case RebootReason::kKernelPanic:
      return fuchsia::feedback::RebootReason::KERNEL_PANIC;
    case RebootReason::kOOM:
      return fuchsia::feedback::RebootReason::SYSTEM_OUT_OF_MEMORY;
    case RebootReason::kHardwareWatchdogTimeout:
      return fuchsia::feedback::RebootReason::HARDWARE_WATCHDOG_TIMEOUT;
    case RebootReason::kSoftwareWatchdogTimeout:
      return fuchsia::feedback::RebootReason::SOFTWARE_WATCHDOG_TIMEOUT;
    case RebootReason::kBrownout:
      return fuchsia::feedback::RebootReason::BROWNOUT;
    case RebootReason::kNotParseable:
      return std::nullopt;
  }
}

}  // namespace last_reboot
}  // namespace forensics
