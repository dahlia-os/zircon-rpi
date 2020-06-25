// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/config.h"

#include <zircon/errors.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace forensics {
namespace feedback_data {
namespace {

void CheckEmptyConfig(const Config& config) {
  EXPECT_TRUE(config.annotation_allowlist.empty());
  EXPECT_TRUE(config.attachment_allowlist.empty());
}

TEST(ConfigTest, ParseConfig_ValidConfig) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/configs/valid.json", &config), ZX_OK);
  EXPECT_THAT(config.annotation_allowlist, testing::UnorderedElementsAreArray({
                                               "foo",
                                           }));
  EXPECT_THAT(config.attachment_allowlist, testing::UnorderedElementsAreArray({
                                               "log.kernel",
                                               "log.syslog",
                                           }));
}

TEST(ConfigTest, ParseConfig_ValidConfigEmptyList) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/configs/valid_empty_list.json", &config), ZX_OK);
  EXPECT_THAT(config.annotation_allowlist, testing::UnorderedElementsAreArray({
                                               "foo",
                                           }));
  EXPECT_TRUE(config.attachment_allowlist.empty());
}

TEST(ConfigTest, ParseConfig_MissingConfig) {
  Config config;
  ASSERT_EQ(ParseConfig("undefined file", &config), ZX_ERR_IO);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_DuplicatedAttachmentKey) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/configs/bad_schema_duplicated_attachment_key.json", &config),
            ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_SpuriousField) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/configs/bad_schema_spurious_field.json", &config),
            ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

TEST(ConfigTest, ParseConfig_BadConfig_MissingRequiredField) {
  Config config;
  ASSERT_EQ(ParseConfig("/pkg/data/configs/bad_schema_missing_required_field.json", &config),
            ZX_ERR_INTERNAL);
  CheckEmptyConfig(config);
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
