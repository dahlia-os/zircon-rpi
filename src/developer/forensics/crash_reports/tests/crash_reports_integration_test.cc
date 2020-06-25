// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/cobalt/test/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/errors.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/fakes/cobalt.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/fsl/vmo/strings.h"

namespace forensics {
namespace crash_reports {
namespace {

using testing::UnorderedElementsAreArray;

class CrashReportsIntegrationTest : public testing::Test {
 public:
  void SetUp() override {
    environment_services_ = sys::ServiceDirectory::CreateFromNamespace();
    environment_services_->Connect(crash_register_.NewRequest());
    environment_services_->Connect(crash_reporter_.NewRequest());
    fake_cobalt_ = std::make_unique<fakes::Cobalt>(environment_services_);
  }

 protected:
  void FileCrashReport() {
    fuchsia::feedback::CrashReport report;
    report.set_program_name("crashing_program");

    fuchsia::feedback::CrashReporter_File_Result out_result;
    ASSERT_EQ(crash_reporter_->File(std::move(report), &out_result), ZX_OK);
    EXPECT_TRUE(out_result.is_response());
  }

  void RegisterProduct() {
    fuchsia::feedback::CrashReportingProduct product;
    product.set_name("some name");
    product.set_version("some version");
    product.set_channel("some channel");

    EXPECT_EQ(crash_register_->Upsert("some/component/URL", std::move(product)), ZX_OK);
  }

 private:
  std::shared_ptr<sys::ServiceDirectory> environment_services_;
  fuchsia::feedback::CrashReportingProductRegisterSyncPtr crash_register_;
  fuchsia::feedback::CrashReporterSyncPtr crash_reporter_;

 protected:
  std::unique_ptr<fakes::Cobalt> fake_cobalt_;
};

// Smoke-tests the actual service for fuchsia.feedback.CrashReportingProductRegister, connecting
// through FIDL.
TEST_F(CrashReportsIntegrationTest, CrashRegister_SmokeTest) { RegisterProduct(); }

// Smoke-tests the actual service for fuchsia.feedback.CrashReporter, connecting through FIDL.
TEST_F(CrashReportsIntegrationTest, CrashReporter_SmokeTest) {
  FileCrashReport();

  EXPECT_THAT(fake_cobalt_->GetAllEventsOfType<cobalt::CrashState>(
                  /*num_expected=*/2, fuchsia::cobalt::test::LogMethod::LOG_EVENT),
              UnorderedElementsAreArray({
                  cobalt::CrashState::kFiled,
                  cobalt::CrashState::kArchived,
              }));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
