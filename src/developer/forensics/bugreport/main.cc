// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "src/developer/forensics/bugreport/bug_reporter.h"
#include "src/developer/forensics/bugreport/command_line_options.h"

int main(int argc, char** argv) {
  using namespace ::forensics::bugreport;

  const Mode mode = ParseModeFromArgcArgv(argc, argv);
  std::set<std::string> attachment_allowlist;
  switch (mode) {
    case Mode::FAILURE:
      return EXIT_FAILURE;
    case Mode::HELP:
      return EXIT_SUCCESS;
    case Mode::DEFAULT:
      auto environment_services = sys::ServiceDirectory::CreateFromNamespace();

      return MakeBugReport(environment_services) ? EXIT_SUCCESS : EXIT_FAILURE;
  }
}
