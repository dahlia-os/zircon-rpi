// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_reporter.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/settings.h"
#include "src/developer/forensics/crash_reports/tests/stub_crash_server.h"
#include "src/developer/forensics/testing/cobalt_test_fixture.h"
#include "src/developer/forensics/testing/fakes/privacy_settings.h"
#include "src/developer/forensics/testing/stubs/channel_provider.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/data_provider.h"
#include "src/developer/forensics/testing/stubs/device_id_provider.h"
#include "src/developer/forensics/testing/stubs/network_reachability_provider.h"
#include "src/developer/forensics/testing/stubs/utc_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace crash_reports {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::CrashReport;
using fuchsia::feedback::GenericCrashReport;
using fuchsia::feedback::NativeCrashReport;
using fuchsia::feedback::RuntimeCrashReport;
using fuchsia::feedback::SpecificCrashReport;
using fuchsia::settings::PrivacySettings;
using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using stubs::UtcProvider;
using testing::ByRef;
using testing::Contains;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::IsSupersetOf;
using testing::Not;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

constexpr bool kUploadSuccessful = true;
constexpr bool kUploadFailed = false;

constexpr char kCrashpadDatabasePath[] = "/tmp/crashes";

// "attachments" should be kept in sync with the value defined in
// //crashpad/client/crash_report_database_generic.cc
constexpr char kCrashpadAttachmentsDir[] = "attachments";
constexpr char kProgramName[] = "crashing_program";

constexpr char kBuildVersion[] = "some-version";
constexpr char kDefaultChannel[] = "some-channel";
constexpr char kDefaultDeviceId[] = "some-device-id";

constexpr char kSingleAttachmentKey[] = "attachment.key";
constexpr char kSingleAttachmentValue[] = "attachment.value";

constexpr bool kUserOptInDataSharing = true;
constexpr bool kUserOptOutDataSharing = false;

constexpr UtcProvider::Response kExternalResponse =
    UtcProvider::Response(UtcProvider::Response::Value::kExternal, zx::nsec(0));

const std::map<std::string, std::string> kDefaultAnnotations = {
    {"feedback.annotation.1.key", "feedback.annotation.1.value"},
    {"feedback.annotation.2.key", "feedback.annotation.2.value"},
};

const std::map<std::string, std::string> kEmptyAnnotations = {};

constexpr char kDefaultAttachmentBundleKey[] = "feedback.attachment.bundle.key";
constexpr char kEmptyAttachmentBundleKey[] = "";

Attachment BuildAttachment(const std::string& key, const std::string& value) {
  Attachment attachment;
  attachment.key = key;
  FX_CHECK(fsl::VmoFromString(value, &attachment.value));
  return attachment;
}

PrivacySettings MakePrivacySettings(const std::optional<bool> user_data_sharing_consent) {
  PrivacySettings settings;
  if (user_data_sharing_consent.has_value()) {
    settings.set_user_data_sharing_consent(user_data_sharing_consent.value());
  }
  return settings;
}

// Unit-tests the implementation of the fuchsia.feedback.CrashReporter FIDL interface.
//
// This does not test the environment service. It directly instantiates the class, without
// connecting through FIDL.
class CrashReporterTest : public UnitTestFixture, public CobaltTestFixture {
 public:
  CrashReporterTest() : UnitTestFixture(), CobaltTestFixture(/*unit_test_fixture=*/this) {}

  void SetUp() override {
    inspector_ = std::make_unique<inspect::Inspector>();
    info_context_ =
        std::make_shared<InfoContext>(&inspector_->GetRoot(), clock_, dispatcher(), services());
    crash_register_ = std::make_unique<CrashRegister>(dispatcher(), services(), info_context_,
                                                      std::string(kBuildVersion));

    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    SetUpNetworkReachabilityProviderServer();
    RunLoopUntilIdle();
  }

  void TearDown() override {
    ASSERT_TRUE(files::DeletePath(kCrashpadDatabasePath, /*recursive=*/true));
  }

 protected:
  // Sets up the underlying crash reporter using the given |config| and |crash_server|.
  void SetUpCrashReporter(Config config, std::unique_ptr<StubCrashServer> crash_server) {
    config_ = std::move(config);
    FX_CHECK((config_.crash_server.url && crash_server) ||
             (!config_.crash_server.url && !crash_server));
    crash_server_ = crash_server.get();

    attachments_dir_ = files::JoinPath(kCrashpadDatabasePath, kCrashpadAttachmentsDir);
    crash_reporter_ = CrashReporter::TryCreate(dispatcher(), services(), clock_, info_context_,
                                               &config_, ErrorOr<std::string>(kBuildVersion),
                                               crash_register_.get(), std::move(crash_server));
    FX_CHECK(crash_reporter_);
  }

  // Sets up the underlying crash reporter using the given |config|.
  void SetUpCrashReporter(Config config) {
    FX_CHECK(!config.crash_server.url);
    return SetUpCrashReporter(std::move(config), /*crash_server=*/nullptr);
  }

  // Sets up the underlying crash reporter using a default config.
  void SetUpCrashReporterDefaultConfig(const std::vector<bool>& upload_attempt_results = {}) {
    SetUpCrashReporter(
        Config{/*crash_server=*/
               {
                   /*upload_policy=*/CrashServerConfig::UploadPolicy::ENABLED,
                   /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
               }},
        std::make_unique<StubCrashServer>(upload_attempt_results));
  }

  void SetUpChannelProviderServer(std::unique_ptr<stubs::ChannelProviderBase> server) {
    channel_provider_server_ = std::move(server);
    if (channel_provider_server_) {
      InjectServiceProvider(channel_provider_server_.get());
    }
  }

  void SetUpDataProviderServer(std::unique_ptr<stubs::DataProviderBase> server) {
    data_provider_server_ = std::move(server);
    if (data_provider_server_) {
      InjectServiceProvider(data_provider_server_.get());
    }
  }

  void SetUpDeviceIdProviderServer(std::unique_ptr<stubs::DeviceIdProviderBase> server) {
    device_id_provider_server_ = std::move(server);
    if (device_id_provider_server_) {
      InjectServiceProvider(device_id_provider_server_.get());
    }
  }

  void SetUpNetworkReachabilityProviderServer() {
    network_reachability_provider_server_ = std::make_unique<stubs::NetworkReachabilityProvider>();
    InjectServiceProvider(network_reachability_provider_server_.get());
  }

  void SetUpPrivacySettingsServer(std::unique_ptr<fakes::PrivacySettings> server) {
    privacy_settings_server_ = std::move(server);
    if (privacy_settings_server_) {
      InjectServiceProvider(privacy_settings_server_.get());
    }
  }

  void SetUpUtcProviderServer(const std::vector<UtcProvider::Response>& responses) {
    utc_provider_server_ = std::make_unique<stubs::UtcProvider>(dispatcher(), responses);
    InjectServiceProvider(utc_provider_server_.get());
  }

  // Checks that in the local Crashpad database there is:
  //   * only one set of attachments
  //   * the set of attachment filenames matches |expected_extra_attachments|
  //   * no attachment is empty
  void CheckAttachmentsInDatabase(
      const std::vector<std::string>& expected_extra_attachment_filenames = {}) {
    const std::vector<std::string> subdirs = GetAttachmentSubdirsInDatabase();
    // We expect a single crash report to have been generated.
    ASSERT_EQ(subdirs.size(), 1u);

    // We expect as attachments the ones returned by the feedback::DataProvider and the extra ones
    // specific to the crash analysis flow under test.
    std::vector<std::string> expected_attachments = expected_extra_attachment_filenames;

    std::vector<std::string> attachments;
    const std::string report_attachments_dir = files::JoinPath(attachments_dir_, subdirs[0]);
    ASSERT_TRUE(files::ReadDirContents(report_attachments_dir, &attachments));
    RemoveCurrentDirectory(&attachments);
    EXPECT_THAT(attachments, UnorderedElementsAreArray(expected_attachments));
    for (const std::string& attachment : attachments) {
      uint64_t size;
      ASSERT_TRUE(files::GetFileSize(files::JoinPath(report_attachments_dir, attachment), &size));
      EXPECT_GT(size, 0u) << "attachment file '" << attachment << "' shouldn't be empty";
    }
  }

  // Checks that on the crash server the annotations received match the concatenation of:
  //   * |expected_extra_annotations|
  //   * default annotations
  //
  // In case of duplicate keys, the value from |expected_extra_annotations| is picked.
  void CheckAnnotationsOnServer(
      const std::map<std::string, std::string>& expected_extra_annotations = {}) {
    ASSERT_TRUE(crash_server_);

    std::map<std::string, testing::Matcher<std::string>> expected_annotations = {
        {"product", "Fuchsia"},
        {"version", kBuildVersion},
        {"ptype", testing::StartsWith("crashing_program")},
        {"osName", "Fuchsia"},
        {"osVersion", kBuildVersion},
        {"reportTimeMillis", Not(IsEmpty())},
        {"guid", kDefaultDeviceId},
        {"channel", kDefaultChannel},
        {"should_process", "false"},
    };
    for (const auto& [key, value] : expected_extra_annotations) {
      expected_annotations[key] = value;
    }

    EXPECT_EQ(crash_server_->latest_annotations().size(), expected_annotations.size());
    for (const auto& [key, value] : expected_annotations) {
      EXPECT_THAT(crash_server_->latest_annotations(),
                  testing::Contains(testing::Pair(key, value)));
    }
  }

  // Checks that on the crash server the keys for the attachments received match the
  // concatenation of:
  //   * |expected_extra_attachment_keys|
  //   * data_provider_->attachment_bundle_key()
  void CheckAttachmentsOnServer(
      const std::vector<std::string>& expected_extra_attachment_keys = {}) {
    ASSERT_TRUE(crash_server_);

    std::vector<std::string> expected_attachment_keys = expected_extra_attachment_keys;

    EXPECT_EQ(crash_server_->latest_attachment_keys().size(), expected_attachment_keys.size());
    for (const auto& key : expected_attachment_keys) {
      EXPECT_THAT(crash_server_->latest_attachment_keys(), testing::Contains(key));
    }
  }

  // Checks that the crash server is still expecting at least one more request.
  //
  // This is useful to check that an upload request hasn't been made as we are using a strict stub.
  void CheckServerStillExpectRequests() {
    ASSERT_TRUE(crash_server_);
    EXPECT_TRUE(crash_server_->ExpectRequest());
  }

  // Files one crash report.
  ::fit::result<void, zx_status_t> FileOneCrashReport(CrashReport report) {
    FX_CHECK(crash_reporter_ != nullptr)
        << "crash_reporter_ is nullptr. Call SetUpCrashReporter() or one of its variants "
           "at the beginning of a test case.";
    ::fit::result<void, zx_status_t> out_result;
    crash_reporter_->File(
        std::move(report),
        [&out_result](::fit::result<void, zx_status_t> result) { out_result = std::move(result); });
    FX_CHECK(RunLoopUntilIdle());
    return out_result;
  }

  // Files one crash report.
  ::fit::result<void, zx_status_t> FileOneCrashReport(
      const std::vector<Annotation>& annotations = {}, std::vector<Attachment> attachments = {}) {
    CrashReport report;
    report.set_program_name(kProgramName);
    if (!annotations.empty()) {
      report.set_annotations(annotations);
    }
    if (!attachments.empty()) {
      report.set_attachments(std::move(attachments));
    }
    return FileOneCrashReport(std::move(report));
  }

  // Files one crash report.
  //
  // |attachment| is useful to control the lower bound of the size of the report by controlling the
  // size of some of the attachment(s). This comes in handy when testing the database size limit
  // enforcement logic for instance.
  ::fit::result<void, zx_status_t> FileOneCrashReportWithSingleAttachment(
      const std::string& attachment = kSingleAttachmentValue) {
    std::vector<Attachment> attachments;
    attachments.emplace_back(BuildAttachment(kSingleAttachmentKey, attachment));
    return FileOneCrashReport(/*annotations=*/{},
                              /*attachments=*/std::move(attachments));
  }

  // Files one generic crash report.
  ::fit::result<void, zx_status_t> FileOneGenericCrashReport(
      const std::optional<std::string>& crash_signature) {
    GenericCrashReport generic_report;
    if (crash_signature.has_value()) {
      generic_report.set_crash_signature(crash_signature.value());
    }

    SpecificCrashReport specific_report;
    specific_report.set_generic(std::move(generic_report));

    CrashReport report;
    report.set_program_name("crashing_program_generic");
    report.set_specific_report(std::move(specific_report));

    return FileOneCrashReport(std::move(report));
  }

  // Files one native crash report.
  ::fit::result<void, zx_status_t> FileOneNativeCrashReport(
      std::optional<fuchsia::mem::Buffer> minidump) {
    NativeCrashReport native_report;
    if (minidump.has_value()) {
      native_report.set_minidump(std::move(minidump.value()));
    }

    SpecificCrashReport specific_report;
    specific_report.set_native(std::move(native_report));

    CrashReport report;
    report.set_program_name("crashing_program_native");
    report.set_specific_report(std::move(specific_report));

    return FileOneCrashReport(std::move(report));
  }

  // Files one Dart crash report.
  ::fit::result<void, zx_status_t> FileOneDartCrashReport(
      const std::optional<std::string>& exception_type,
      const std::optional<std::string>& exception_message,
      std::optional<fuchsia::mem::Buffer> exception_stack_trace) {
    RuntimeCrashReport dart_report;
    if (exception_type.has_value()) {
      dart_report.set_exception_type(exception_type.value());
    }
    if (exception_message.has_value()) {
      dart_report.set_exception_message(exception_message.value());
    }
    if (exception_stack_trace.has_value()) {
      dart_report.set_exception_stack_trace(std::move(exception_stack_trace.value()));
    }

    SpecificCrashReport specific_report;
    specific_report.set_dart(std::move(dart_report));

    CrashReport report;
    report.set_program_name("crashing_program_dart");
    report.set_specific_report(std::move(specific_report));

    return FileOneCrashReport(std::move(report));
  }

  // Files one empty crash report.
  ::fit::result<void, zx_status_t> FileOneEmptyCrashReport() {
    CrashReport report;
    return FileOneCrashReport(std::move(report));
  }

  void SetPrivacySettings(std::optional<bool> user_data_sharing_consent) {
    FX_CHECK(privacy_settings_server_);

    ::fit::result<void, fuchsia::settings::Error> set_result;
    privacy_settings_server_->Set(
        MakePrivacySettings(user_data_sharing_consent),
        [&set_result](::fit::result<void, fuchsia::settings::Error> result) {
          set_result = std::move(result);
        });
    EXPECT_TRUE(set_result.is_ok());
  }

  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FX_CHECK(result.is_ok());
    return result.take_value();
  }

 private:
  // Returns all the attachment subdirectories under the over-arching attachment directory in the
  // database.
  //
  // Each subdirectory corresponds to one local crash report.
  std::vector<std::string> GetAttachmentSubdirsInDatabase() {
    std::vector<std::string> subdirs;
    FX_CHECK(files::ReadDirContents(attachments_dir_, &subdirs));
    RemoveCurrentDirectory(&subdirs);
    return subdirs;
  }

  void RemoveCurrentDirectory(std::vector<std::string>* dirs) {
    dirs->erase(std::remove(dirs->begin(), dirs->end(), "."), dirs->end());
  }

 private:
  // Stubs and fake servers.
  std::unique_ptr<stubs::ChannelProviderBase> channel_provider_server_;
  std::unique_ptr<stubs::DataProviderBase> data_provider_server_;
  std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server_;
  std::unique_ptr<stubs::NetworkReachabilityProvider> network_reachability_provider_server_;
  std::unique_ptr<fakes::PrivacySettings> privacy_settings_server_;
  std::unique_ptr<stubs::UtcProviderBase> utc_provider_server_;

 protected:
  StubCrashServer* crash_server_;

 private:
  std::string attachments_dir_;
  std::unique_ptr<inspect::Inspector> inspector_;
  timekeeper::TestClock clock_;
  std::shared_ptr<InfoContext> info_context_;
  Config config_;

 protected:
  std::unique_ptr<CrashRegister> crash_register_;
  std::unique_ptr<CrashReporter> crash_reporter_;
};

TEST_F(CrashReporterTest, Succeed_OnInputCrashReport) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kDefaultAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAnnotationsOnServer(kDefaultAnnotations);
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Check_UTCTimeIsNotReady) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kDefaultAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({
      UtcProvider::Response(UtcProvider::Response::Value::kBackstop),
      UtcProvider::Response(UtcProvider::Response::Value::kNoResponse),
  });

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});

  EXPECT_EQ(crash_server_->latest_annotations().find("reportTimeMillis"),
            crash_server_->latest_annotations().end());

  ASSERT_NE(crash_server_->latest_annotations().find("debug.report-time.set"),
            crash_server_->latest_annotations().end());
  EXPECT_EQ(crash_server_->latest_annotations().at("debug.report-time.set"), "false");
}

TEST_F(CrashReporterTest, Check_guidNotSet) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kDefaultAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProviderReturnsError>());
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});

  EXPECT_EQ(crash_server_->latest_annotations().find("guid"),
            crash_server_->latest_annotations().end());

  ASSERT_NE(crash_server_->latest_annotations().find("debug.guid.set"),
            crash_server_->latest_annotations().end());
  EXPECT_EQ(crash_server_->latest_annotations().at("debug.guid.set"), "false");

  ASSERT_NE(crash_server_->latest_annotations().find("debug.device-id.error"),
            crash_server_->latest_annotations().end());
  EXPECT_EQ(crash_server_->latest_annotations().at("debug.device-id.error"), "missing");
}

TEST_F(CrashReporterTest, Check_UnknownChannel) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderClosesConnection>());
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});

  ASSERT_NE(crash_server_->latest_annotations().find("channel"),
            crash_server_->latest_annotations().end());
  EXPECT_EQ(crash_server_->latest_annotations().at("channel"), "<unknown>");

  ASSERT_NE(crash_server_->latest_annotations().find("debug.channel.error"),
            crash_server_->latest_annotations().end());
  EXPECT_EQ(crash_server_->latest_annotations().at("debug.channel.error"), "FIDL connection error");
}

TEST_F(CrashReporterTest, Check_RegisteredProduct) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  fuchsia::feedback::CrashReportingProduct product;
  product.set_name("some name");
  product.set_version("some version");
  product.set_channel("some channel");
  crash_register_->Upsert(kProgramName, std::move(product));

  ASSERT_TRUE(FileOneCrashReport().is_ok());

  ASSERT_NE(crash_server_->latest_annotations().find("product"),
            crash_server_->latest_annotations().end());
  EXPECT_EQ(crash_server_->latest_annotations().at("product"), "some name");
  ASSERT_NE(crash_server_->latest_annotations().find("version"),
            crash_server_->latest_annotations().end());
  EXPECT_EQ(crash_server_->latest_annotations().at("version"), "some version");
  ASSERT_NE(crash_server_->latest_annotations().find("channel"),
            crash_server_->latest_annotations().end());
  EXPECT_EQ(crash_server_->latest_annotations().at("channel"), "some channel");
}

TEST_F(CrashReporterTest, Succeed_OnInputCrashReportWithAdditionalData) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kEmptyAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  std::vector<Attachment> attachments;
  attachments.emplace_back(BuildAttachment(kSingleAttachmentKey, kSingleAttachmentValue));

  ASSERT_TRUE(FileOneCrashReport(
                  /*annotations=*/
                  {
                      {"annotation.key", "annotation.value"},
                  },
                  /*attachments=*/std::move(attachments))
                  .is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer({
      {"annotation.key", "annotation.value"},
  });
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashReporterTest, Succeed_OnInputCrashReportWithEventId) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  CrashReport report;
  report.set_program_name(kProgramName);
  report.set_event_id("some-event-id");

  ASSERT_TRUE(FileOneCrashReport(std::move(report)).is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAnnotationsOnServer({
      {"comments", "some-event-id"},
  });
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Succeed_OnInputCrashReportWithProgramUptime) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  CrashReport report;
  report.set_program_name(kProgramName);
  const zx::duration uptime =
      zx::hour(3) * 24 + zx::hour(15) + zx::min(33) + zx::sec(17) + zx::msec(54);
  report.set_program_uptime(uptime.get());

  ASSERT_TRUE(FileOneCrashReport(std::move(report)).is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAnnotationsOnServer({
      {"ptime", std::to_string(uptime.to_msecs())},
  });
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Succeed_OnGenericInputCrashReport) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kDefaultAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneGenericCrashReport(std::nullopt).is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAnnotationsOnServer(kDefaultAnnotations);
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Succeed_OnGenericInputCrashReportWithSignature) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneGenericCrashReport("some-signature").is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAnnotationsOnServer({
      {"signature", "some-signature"},
  });
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Succeed_OnNativeInputCrashReport) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  fuchsia::mem::Buffer minidump;
  fsl::VmoFromString("minidump", &minidump);

  ASSERT_TRUE(FileOneNativeCrashReport(std::move(minidump)).is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAnnotationsOnServer({
      {"should_process", "true"},
  });
  CheckAttachmentsOnServer({"uploadFileMinidump", kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Succeed_OnNativeInputCrashReportWithoutMinidump) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneNativeCrashReport(std::nullopt).is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAnnotationsOnServer({
      {"signature", "fuchsia-no-minidump"},
  });
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Succeed_OnDartInputCrashReport) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kEmptyAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  fuchsia::mem::Buffer stack_trace;
  fsl::VmoFromString("#0", &stack_trace);

  ASSERT_TRUE(
      FileOneDartCrashReport("FileSystemException", "cannot open file", std::move(stack_trace))
          .is_ok());
  CheckAttachmentsInDatabase({"DartError"});
  CheckAnnotationsOnServer({
      {"error_runtime_type", "FileSystemException"},
      {"error_message", "cannot open file"},
      {"type", "DartError"},
      {"should_process", "true"},
  });
  CheckAttachmentsOnServer({"DartError"});
}

TEST_F(CrashReporterTest, Succeed_OnDartInputCrashReportWithoutExceptionData) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneDartCrashReport(std::nullopt, std::nullopt, std::nullopt).is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAnnotationsOnServer({
      {"type", "DartError"},
      {"signature", "fuchsia-no-dart-stack-trace"},
  });
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Fail_OnInvalidInputCrashReport) {
  SetUpCrashReporterDefaultConfig();
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneEmptyCrashReport().is_error());
}

TEST_F(CrashReporterTest, Upload_OnUserAlreadyOptedInDataSharing) {
  SetUpCrashReporter(
      Config{/*crash_server=*/
             {
                 /*upload_policy=*/CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS,
                 /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
             }},
      std::make_unique<StubCrashServer>(std::vector<bool>({kUploadSuccessful})));
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kDefaultAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpPrivacySettingsServer(std::make_unique<fakes::PrivacySettings>());
  SetPrivacySettings(kUserOptInDataSharing);
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckAnnotationsOnServer(kDefaultAnnotations);
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Archive_OnUserAlreadyOptedOutDataSharing) {
  SetUpCrashReporter(
      Config{/*crash_server=*/
             {
                 /*upload_policy=*/CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS,
                 /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
             }},
      std::make_unique<StubCrashServer>(std::vector<bool>({})));
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpPrivacySettingsServer(std::make_unique<fakes::PrivacySettings>());
  SetPrivacySettings(kUserOptOutDataSharing);
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Upload_OnceUserOptInDataSharing) {
  SetUpCrashReporter(
      Config{/*crash_server=*/
             {
                 /*upload_policy=*/CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS,
                 /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
             }},
      std::make_unique<StubCrashServer>(std::vector<bool>({kUploadSuccessful})));
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kDefaultAnnotations, kDefaultAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpPrivacySettingsServer(std::make_unique<fakes::PrivacySettings>());
  SetUpUtcProviderServer({kExternalResponse});

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase({kDefaultAttachmentBundleKey});
  CheckServerStillExpectRequests();

  SetPrivacySettings(kUserOptInDataSharing);
  ASSERT_TRUE(RunLoopUntilIdle());

  CheckAnnotationsOnServer(kDefaultAnnotations);
  CheckAttachmentsOnServer({kDefaultAttachmentBundleKey});
}

TEST_F(CrashReporterTest, Succeed_OnConcurrentReports) {
  SetUpCrashReporterDefaultConfig(std::vector<bool>(10, kUploadSuccessful));
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kEmptyAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  // We generate ten crash reports before runnning the loop to make sure that one crash
  // report filing doesn't clean up the concurrent crash reports being filed.
  const size_t kNumReports = 10;

  std::vector<::fit::result<void, zx_status_t>> results;
  for (size_t i = 0; i < kNumReports; ++i) {
    CrashReport report;
    report.set_program_name(kProgramName);
    crash_reporter_->File(std::move(report), [&results](::fit::result<void, zx_status_t> result) {
      results.push_back(result);
    });
  }

  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_EQ(results.size(), kNumReports);
  for (const auto result : results) {
    EXPECT_TRUE(result.is_ok());
  }
}

TEST_F(CrashReporterTest, Succeed_OnFailedUpload) {
  SetUpCrashReporter(
      Config{/*crash_server=*/
             {
                 /*upload_policy=*/CrashServerConfig::UploadPolicy::ENABLED,
                 /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
             }},
      std::make_unique<StubCrashServer>(std::vector<bool>({kUploadFailed})));
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kEmptyAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneCrashReport().is_ok());
}

TEST_F(CrashReporterTest, Succeed_OnDisabledUpload) {
  SetUpCrashReporter(Config{/*crash_server=*/
                            {
                                /*upload_policy=*/CrashServerConfig::UploadPolicy::DISABLED,
                                /*url=*/nullptr,
                            }});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kEmptyAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneCrashReport().is_ok());
}

TEST_F(CrashReporterTest, Succeed_OnNoFeedbackAttachments) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProviderReturnsNoAttachment>(kDefaultAnnotations));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer(kDefaultAnnotations);
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashReporterTest, Succeed_OnNoFeedbackAnnotations) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProviderReturnsNoAnnotation>(kEmptyAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashReporterTest, Succeed_OnNoFeedbackData) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(std::make_unique<stubs::DataProviderReturnsEmptyBugreport>());
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer({
      {"debug.bugreport.empty", "true"},
  });
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashReporterTest, Succeed_OnDataProviderNotServing) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(nullptr);
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer({
      {"debug.bugreport.error", "FIDL connection error"},
  });
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashReporterTest, Check_CobaltAfterSuccessfulUpload) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kEmptyAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneCrashReport().is_ok());

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kFiled),
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
              }));
}

TEST_F(CrashReporterTest, Check_CobaltAfterInvalidInputCrashReport) {
  SetUpCrashReporterDefaultConfig();
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneEmptyCrashReport().is_error());
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::CrashState::kDropped),
                                      }));
}

TEST_F(CrashReporterTest, Check_InspectTreeAfterSuccessfulUpload) {
  SetUpCrashReporterDefaultConfig({kUploadSuccessful});
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>(kDefaultChannel));
  SetUpDataProviderServer(
      std::make_unique<stubs::DataProvider>(kEmptyAnnotations, kEmptyAttachmentBundleKey));
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  SetUpUtcProviderServer({kExternalResponse});

  EXPECT_TRUE(FileOneCrashReport().is_ok());
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("crash_reporter")),
                        ChildrenMatch(IsSupersetOf({
                            AllOf(NodeMatches(NameMatches("reports")),
                                  ChildrenMatch(ElementsAre(
                                      AllOf(NodeMatches(NameMatches(kProgramName)),
                                            ChildrenMatch(ElementsAre(AllOf(
                                                NodeMatches(PropertyList(UnorderedElementsAreArray({
                                                    StringIs("creation_time", Not(IsEmpty())),
                                                    StringIs("final_state", "uploaded"),
                                                    UintIs("upload_attempts", 1u),
                                                }))),
                                                ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                                                    NameMatches("crash_server"),
                                                    PropertyList(UnorderedElementsAreArray({
                                                        StringIs("creation_time", Not(IsEmpty())),
                                                        StringIs("id", kStubServerReportId),
                                                    }))))))))))))),
                            AllOf(NodeMatches(AllOf(NameMatches("queue"),
                                                    PropertyList(ElementsAre(UintIs("size", 0u))))),
                                  ChildrenMatch(IsEmpty())),
                        }))))));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
