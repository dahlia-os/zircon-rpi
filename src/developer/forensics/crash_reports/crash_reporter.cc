// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_reporter.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/crash_server.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/crash_reports/report_util.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/timeout.h"

namespace forensics {
namespace crash_reports {
namespace {

using fuchsia::feedback::Bugreport;
using fuchsia::feedback::CrashReport;

constexpr zx::duration kChannelOrDeviceIdTimeout = zx::sec(30);
constexpr zx::duration kBugreportTimeout = zx::min(2);

}  // namespace

std::unique_ptr<CrashReporter> CrashReporter::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const timekeeper::Clock& clock, std::shared_ptr<InfoContext> info_context, const Config* config,
    const ErrorOr<std::string>& build_version, CrashRegister* crash_register) {
  std::unique_ptr<CrashServer> crash_server;
  if (config->crash_server.url) {
    crash_server = std::make_unique<CrashServer>(*(config->crash_server.url));
  }

  return TryCreate(dispatcher, std::move(services), clock, std::move(info_context), config,
                   build_version, crash_register, std::move(crash_server));
}

std::unique_ptr<CrashReporter> CrashReporter::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const timekeeper::Clock& clock, std::shared_ptr<InfoContext> info_context, const Config* config,
    const ErrorOr<std::string>& build_version, CrashRegister* crash_register,
    std::unique_ptr<CrashServer> crash_server) {
  auto queue = Queue::TryCreate(dispatcher, services, info_context, crash_server.get());
  if (!queue) {
    FX_LOGS(FATAL) << "Failed to set up crash reporter";
    return nullptr;
  }

  return std::unique_ptr<CrashReporter>(new CrashReporter(
      dispatcher, std::move(services), clock, std::move(info_context), std::move(config),
      build_version, crash_register, std::move(crash_server), std::move(queue)));
}

CrashReporter::CrashReporter(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const timekeeper::Clock& clock, std::shared_ptr<InfoContext> info_context, const Config* config,
    const ErrorOr<std::string>& build_version, CrashRegister* crash_register,
    std::unique_ptr<CrashServer> crash_server, std::unique_ptr<Queue> queue)
    : dispatcher_(dispatcher),
      executor_(dispatcher),
      services_(services),
      config_(std::move(config)),
      build_version_(build_version),
      crash_register_(crash_register),
      utc_provider_(services_, clock),
      crash_server_(std::move(crash_server)),
      queue_(std::move(queue)),
      info_(std::move(info_context)),
      privacy_settings_watcher_(dispatcher, services_, &settings_),
      data_provider_ptr_(dispatcher_, services_),
      device_id_provider_ptr_(dispatcher_, services_) {
  FX_CHECK(dispatcher_);
  FX_CHECK(services_);
  FX_CHECK(crash_register_);
  if (config->crash_server.url) {
    FX_CHECK(crash_server_);
  }
  FX_CHECK(queue_);

  const auto& upload_policy = config_->crash_server.upload_policy;
  settings_.set_upload_policy(upload_policy);
  if (upload_policy == CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS) {
    privacy_settings_watcher_.StartWatching();
  }

  queue_->WatchSettings(&settings_);

  info_.ExposeSettings(&settings_);
}

void CrashReporter::File(fuchsia::feedback::CrashReport report, FileCallback callback) {
  if (!report.has_program_name()) {
    FX_LOGS(ERROR) << "Input report missing required program name. Won't file.";
    callback(::fit::error(ZX_ERR_INVALID_ARGS));
    info_.LogCrashState(cobalt::CrashState::kDropped);
    return;
  }
  const std::string program_name = report.program_name();
  FX_LOGS(INFO) << "Generating report for '" << program_name << "'";

  auto bugreport_promise = data_provider_ptr_.GetBugreport(kBugreportTimeout);
  auto device_id_promise = device_id_provider_ptr_.GetId(kChannelOrDeviceIdTimeout);
  auto product_promise =
      crash_register_->GetProduct(program_name, fit::Timeout(kChannelOrDeviceIdTimeout));

  auto promise =
      ::fit::join_promises(std::move(bugreport_promise), std::move(device_id_promise),
                           std::move(product_promise))
          .then(
              [this, report = std::move(report), program_name](
                  ::fit::result<
                      std::tuple<::fit::result<Bugreport, Error>, ::fit::result<std::string, Error>,
                                 ::fit::result<Product>>>& results) mutable -> ::fit::result<void> {
                if (results.is_error()) {
                  return ::fit::error();
                }

                auto bugreport = std::move(std::get<0>(results.value()));
                auto device_id = std::move(std::get<1>(results.value()));
                auto product = std::move(std::get<2>(results.value()));

                if (product.is_error()) {
                  return ::fit::error();
                }

                std::map<std::string, std::string> annotations;
                std::map<std::string, fuchsia::mem::Buffer> attachments;
                std::optional<fuchsia::mem::Buffer> minidump;
                BuildAnnotationsAndAttachments(
                    std::move(report), std::move(bugreport), utc_provider_.CurrentTime(), device_id,
                    build_version_, product.value(), &annotations, &attachments, &minidump);

                if (!queue_->Add(program_name, std::move(attachments), std::move(minidump),
                                 annotations)) {
                  FX_LOGS(ERROR) << "Error adding new report to the queue";
                  info_.LogCrashState(cobalt::CrashState::kDropped);
                  return ::fit::error();
                }

                info_.LogCrashState(cobalt::CrashState::kFiled);
                return ::fit::ok();
              })
          .then([callback = std::move(callback)](::fit::result<void>& result) {
            if (result.is_error()) {
              FX_LOGS(ERROR) << "Failed to file report. Won't retry.";
              callback(::fit::error(ZX_ERR_INTERNAL));
            } else {
              callback(::fit::ok());
            }
          });

  executor_.schedule_task(std::move(promise));
}

}  // namespace crash_reports
}  // namespace forensics
