// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_QUEUE_INFO_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_QUEUE_INFO_H_

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/developer/forensics/crash_reports/info/info_context.h"

namespace forensics {
namespace crash_reports {

// Information about the queue we want to export.
struct QueueInfo {
 public:
  QueueInfo(std::shared_ptr<InfoContext> context);

  void LogReport(const std::string& program_name, const std::string& local_report_id);
  void SetSize(uint64_t size);

 private:
  std::shared_ptr<InfoContext> context_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_QUEUE_INFO_H_
