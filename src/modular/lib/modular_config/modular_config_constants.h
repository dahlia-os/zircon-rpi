// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_CONSTANTS_H_
#define SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_CONSTANTS_H_

namespace modular_config {

constexpr char kBasemgrConfigName[] = "basemgr";
constexpr char kSessionmgrConfigName[] = "sessionmgr";
constexpr char kSessionmgrUrl[] = "fuchsia-pkg://fuchsia.com/sessionmgr#meta/sessionmgr.cmx";

constexpr char kDefaultConfigDir[] = "/config/data";
constexpr char kOverriddenConfigDir[] = "/config_override/data";

// This file path is rooted at either |kDefaultConfigDir| or
// |kOverriddenConfigDir|
constexpr char kStartupConfigFilePath[] = "startup.config";

constexpr char kTrue[] = "true";

// Used by sessionmgr component_args and base shell.
constexpr char kArgs[] = "args";

// Presentation constants
constexpr char kDisplayUsage[] = "display_usage";
constexpr char kHandheld[] = "handheld";
constexpr char kClose[] = "close";
constexpr char kNear[] = "near";
constexpr char kMidrange[] = "midrange";
constexpr char kFar[] = "far";
constexpr char kUnknown[] = "unknown";
constexpr char kScreenHeight[] = "screen_height";
constexpr char kScreenWidth[] = "screen_width";

// Basemgr constants
constexpr char kEnableCobalt[] = "enable_cobalt";
constexpr char kUseSessionShellForStoryShellFactory[] = "use_session_shell_for_story_shell_factory";

// Sessionmgr constants
constexpr char kComponentArgs[] = "component_args";
constexpr char kAgentServiceIndex[] = "agent_service_index";
constexpr char kServiceName[] = "service_name";
constexpr char kAgentUrl[] = "agent_url";
constexpr char kUri[] = "uri";
constexpr char kStartupAgents[] = "startup_agents";
constexpr char kSessionAgents[] = "session_agents";
constexpr char kRestartSessionOnAgentCrash[] = "restart_session_on_agent_crash";

// Inspect property constants
constexpr char kInspectModuleSource[] = "module_source";
constexpr char kInspectIsEmbedded[] = "is_embedded";
constexpr char kInspectIntentAction[] = "intent_action";
constexpr char kInspectIsDeleted[] = "is_deleted";
constexpr char kInspectSurfaceRelationArrangement[] = "surface_arrangement";
constexpr char kInspectSurfaceRelationDependency[] = "surface_dependency";
constexpr char kInspectSurfaceRelationEmphasis[] = "surface_emphasis";
constexpr char kInspectModulePath[] = "module_path";

// Shell constants
inline constexpr char kDefaultBaseShellUrl[] =
    "fuchsia-pkg://fuchsia.com/auto_login_base_shell#meta/"
    "auto_login_base_shell.cmx";
inline constexpr char kDefaultSessionShellUrl[] =
    "fuchsia-pkg://fuchsia.com/ermine_session_shell#meta/"
    "ermine_session_shell.cmx";
constexpr char kDefaultStoryShellUrl[] = "fuchsia-pkg://fuchsia.com/mondrian#meta/mondrian.cmx";
constexpr char kBaseShell[] = "base_shell";
constexpr char kKeepAliveAfterLogin[] = "keep_alive_after_login";
constexpr char kName[] = "name";
constexpr char kUrl[] = "url";
constexpr char kSessionShells[] = "session_shells";
constexpr char kStoryShellUrl[] = "story_shell_url";

}  // namespace modular_config

#endif  // SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_CONSTANTS_H_
