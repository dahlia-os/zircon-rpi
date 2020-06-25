// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_I18N_TESTS_MOCKS_MOCK_MESSAGE_FORMATTER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_I18N_TESTS_MOCKS_MOCK_MESSAGE_FORMATTER_H_

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/ui/a11y/lib/screen_reader/i18n/message_formatter.h"

namespace accessibility_test {

class MockMessageFormatter : public a11y::i18n::MessageFormatter {
 public:
  MockMessageFormatter();
  ~MockMessageFormatter() override = default;

  // Sets the |message| that will be returned for |id| when FormatStringById() is called.
  void SetMessageForId(const uint64_t id, std::string message);

  // |MessageFormatter|
  std::optional<std::string> FormatStringById(
      const uint64_t id, const std::vector<std::string>& arg_names = std::vector<std::string>(),
      const std::vector<std::string>& arg_values = std::vector<std::string>()) override;

 private:
  std::unordered_map<uint64_t, std::string> id_to_message_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_I18N_TESTS_MOCKS_MOCK_MESSAGE_FORMATTER_H_
