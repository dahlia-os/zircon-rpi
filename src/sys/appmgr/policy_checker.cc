// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/policy_checker.h"

#include "src/sys/appmgr/allow_list.h"

namespace component {
namespace {

constexpr char kDeprecatedShellAllowList[] = "allowlist/deprecated_shell.txt";
constexpr char kDeprecatedAmbientReplaceAsExecAllowList[] =
    "allowlist/deprecated_ambient_replace_as_executable.txt";
constexpr char kComponentEventProviderAllowList[] = "allowlist/component_event_provider.txt";
constexpr char kEventSourceAllowList[] = "allowlist/event_source.txt";
constexpr char kHubAllowList[] = "allowlist/hub.txt";
constexpr char kPackageResolverAllowList[] = "allowlist/package_resolver.txt";
constexpr char kPackageCacheAllowList[] = "allowlist/package_cache.txt";
constexpr char kPkgFsVersionsAllowList[] = "allowlist/pkgfs_versions.txt";
constexpr char kRootJobAllowList[] = "allowlist/root_job.txt";
constexpr char kRootResourceAllowList[] = "allowlist/root_resource.txt";

}  // end of namespace.

PolicyChecker::PolicyChecker(fxl::UniqueFD config) : config_(std::move(config)) {}

std::optional<SecurityPolicy> PolicyChecker::Check(const SandboxMetadata& sandbox,
                                                   const FuchsiaPkgUrl& pkg_url) {
  SecurityPolicy policy;
  if (sandbox.HasService("fuchsia.sys.internal.ComponentEventProvider")) {
    if (!CheckComponentEventProvider(pkg_url)) {
      FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                     << "fuchsia.sys.internal.ComponentEventProvider";
      return std::nullopt;
    }
    policy.enable_component_event_provider = true;
  }
  if (sandbox.HasFeature("deprecated-ambient-replace-as-executable")) {
    if (!CheckDeprecatedAmbientReplaceAsExecutable(pkg_url)) {
      FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                     << "deprecated-ambient-replace-as-executable. go/fx-hermetic-sandboxes";
      return std::nullopt;
    }
    policy.enable_ambient_executable = true;
  }
  if (sandbox.HasFeature("deprecated-shell") && !CheckDeprecatedShell(pkg_url)) {
    FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                   << "deprecated-shell. go/fx-hermetic-sandboxes";
    return std::nullopt;
  }
  if (sandbox.HasFeature("hub") && !CheckHub(pkg_url)) {
    FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                   << "hub. go/no-hub";
    return std::nullopt;
  }
  if (sandbox.HasService("fuchsia.sys2.EventSource") && !CheckEventSource(pkg_url)) {
    FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                   << "fuchsia.sys2.EventSource";
    return std::nullopt;
  }
  if (sandbox.HasService("fuchsia.pkg.PackageResolver") && !CheckPackageResolver(pkg_url)) {
    FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                   << "fuchsia.pkg.PackageResolver. go/no-package-resolver";
    return std::nullopt;
  }
  if (sandbox.HasService("fuchsia.pkg.PackageCache") && !CheckPackageCache(pkg_url)) {
    FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                   << "fuchsia.pkg.PackageCache. go/no-package-cache";
    return std::nullopt;
  }
  if (sandbox.HasPkgFsPath("versions") && !CheckPkgFsVersions(pkg_url)) {
    FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                   << "pkgfs/versions. go/no-pkgfs-versions";
    return std::nullopt;
  }
  if (sandbox.HasService("fuchsia.boot.RootJob") && !CheckRootJob(pkg_url)) {
    FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                   << "fuchsia.boot.RootJob";
    return std::nullopt;
  }
  if (sandbox.HasService("fuchsia.boot.RootResource") && !CheckRootResource(pkg_url)) {
    FX_LOGS(ERROR) << "Component " << pkg_url.ToString() << " is not allowed to use "
                   << "fuchsia.boot.RootResource";
    return std::nullopt;
  }
  return policy;
}

bool PolicyChecker::CheckDeprecatedAmbientReplaceAsExecutable(const FuchsiaPkgUrl& pkg_url) {
  AllowList deprecated_exec_allowlist(config_, kDeprecatedAmbientReplaceAsExecAllowList);
  return deprecated_exec_allowlist.IsAllowed(pkg_url);
}

bool PolicyChecker::CheckComponentEventProvider(const FuchsiaPkgUrl& pkg_url) {
  AllowList component_event_provider_allowlist(config_, kComponentEventProviderAllowList);
  return component_event_provider_allowlist.IsAllowed(pkg_url);
}

bool PolicyChecker::CheckEventSource(const FuchsiaPkgUrl& pkg_url) {
  AllowList event_source_allowlist(config_, kEventSourceAllowList);
  return event_source_allowlist.IsAllowed(pkg_url);
}

bool PolicyChecker::CheckDeprecatedShell(const FuchsiaPkgUrl& pkg_url) {
  AllowList deprecated_shell_allowlist(config_, kDeprecatedShellAllowList);
  return deprecated_shell_allowlist.IsAllowed(pkg_url);
}

bool PolicyChecker::CheckHub(const FuchsiaPkgUrl& pkg_url) {
  AllowList hub_allowlist(config_, kHubAllowList);
  return hub_allowlist.IsAllowed(pkg_url);
}

bool PolicyChecker::CheckPackageResolver(const FuchsiaPkgUrl& pkg_url) {
  AllowList package_resolver_allowlist(config_, kPackageResolverAllowList);
  return package_resolver_allowlist.IsAllowed(pkg_url);
}

bool PolicyChecker::CheckPackageCache(const FuchsiaPkgUrl& pkg_url) {
  AllowList package_cache_allowlist(config_, kPackageCacheAllowList);
  return package_cache_allowlist.IsAllowed(pkg_url);
}

bool PolicyChecker::CheckPkgFsVersions(const FuchsiaPkgUrl& pkg_url) {
  AllowList pkgfs_versions_allowlist(config_, kPkgFsVersionsAllowList);
  return pkgfs_versions_allowlist.IsAllowed(pkg_url);
}

bool PolicyChecker::CheckRootJob(const FuchsiaPkgUrl& pkg_url) {
  AllowList root_job_allowlist(config_, kRootJobAllowList);
  return root_job_allowlist.IsAllowed(pkg_url);
}

bool PolicyChecker::CheckRootResource(const FuchsiaPkgUrl& pkg_url) {
  AllowList root_resource_allowlist(config_, kRootResourceAllowList);
  return root_resource_allowlist.IsAllowed(pkg_url);
}

}  // namespace component
