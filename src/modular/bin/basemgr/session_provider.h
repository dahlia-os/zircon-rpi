// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_SESSION_PROVIDER_H_
#define SRC_MODULAR_BIN_BASEMGR_SESSION_PROVIDER_H_

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_dir.h>

#include "src/lib/intl/intl_property_provider_impl/intl_property_provider_impl.h"
#include "src/modular/bin/basemgr/session_context_impl.h"
#include "src/modular/lib/async/cpp/future.h"

namespace modular {

class SessionProvider {
 public:
  // Users of SessionProvider must register a Delegate object, which provides
  // functionality to SessionProvider that's outside the scope of this class.
  class Delegate {
   public:
    // Called when a session provided by SessionProvider wants to acquire
    // presentation.
    virtual void GetPresentation(
        fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) = 0;
  };

  // Target constructor.
  //
  // |on_zero_sessions| is invoked when all sessions have been deleted. This is
  // meant to be a callback for BasemgrImpl to start a new session.
  SessionProvider(Delegate* const delegate, fuchsia::sys::Launcher* const launcher,
                  fuchsia::hardware::power::statecontrol::AdminPtr administrator,
                  fuchsia::modular::AppConfig sessionmgr, fuchsia::modular::AppConfig session_shell,
                  fuchsia::modular::AppConfig story_shell,
                  bool use_session_shell_for_story_shell_factory,
                  std::unique_ptr<intl::IntlPropertyProviderImpl> intl_property_provider,
                  fuchsia::modular::session::ModularConfig config,
                  fit::function<void()> on_zero_sessions);

  // Starts a new sessionmgr process if there isn't one already. Returns false
  // if there is an existing sessionmgr process, and does not start a new
  // session. Returns true if a new session was started successfully.
  bool StartSession(fuchsia::ui::views::ViewToken view_token, bool use_random_id);

  // Asynchronously tears down the sessionmgr process. |callback| is invoked
  // once teardown is complete or has timed out.
  void Teardown(fit::function<void()> callback);

  // Callback function for session_provider to invoke when there is no active
  // session
  void OnSessionShutdown(SessionContextImpl::ShutDownReason shutdown_reason);

  // Shuts down the running session, causing a new session to be created.
  void RestartSession(fit::function<void()> on_restart_complete);

 private:
  Delegate* const delegate_;                // Neither owned nor copied.
  fuchsia::sys::Launcher* const launcher_;  // Not owned.
  fuchsia::hardware::power::statecontrol::AdminPtr administrator_;
  const fuchsia::modular::AppConfig sessionmgr_;
  const fuchsia::modular::AppConfig session_shell_;
  const fuchsia::modular::AppConfig story_shell_;
  bool use_session_shell_for_story_shell_factory_;

  // The number of times that session had to be recovered from a crash, during a
  // given timeout. If the count exceed the max retry limit, a device
  // reboot will be triggered
  int session_crash_recovery_counter_ = 0;

  // The timestamp of when last crash happened
  zx::time last_crash_time_;

  fit::function<void()> on_zero_sessions_;
  std::unique_ptr<SessionContextImpl> session_context_;

  // Service directory from which `fuchsia.intl.PropertyProvider` and others
  // will be served to child `Sessionmgr`s.
  vfs::PseudoDir sessionmgr_service_dir_;
  std::unique_ptr<intl::IntlPropertyProviderImpl> intl_property_provider_;

  // A copy of the modular configuration to be served to sessionmgr.
  fuchsia::modular::session::ModularConfig config_;

  // A directory used to serve configurations to sessionmgr.
  std::unique_ptr<vfs::PseudoDir> config_dir_;

  // A channel shared with sessionmgr for configurations.
  zx::channel config_request_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionProvider);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_SESSION_PROVIDER_H_
