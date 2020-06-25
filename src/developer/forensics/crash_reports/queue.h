// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_QUEUE_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_QUEUE_H_

#include <fuchsia/net/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include <map>
#include <vector>

#include "src/developer/forensics/crash_reports/crash_server.h"
#include "src/developer/forensics/crash_reports/database.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/info/queue_info.h"
#include "src/developer/forensics/crash_reports/settings.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/util/misc/uuid.h"

namespace forensics {
namespace crash_reports {

// Queues pending reports and processes them according to its internal State.
class Queue {
 public:
  static std::unique_ptr<Queue> TryCreate(async_dispatcher_t* dispatcher,
                                          std::shared_ptr<sys::ServiceDirectory> services,
                                          std::shared_ptr<InfoContext> info_context,
                                          CrashServer* crash_server);

  // Allow the queue's functionality to change based on the upload policy.
  void WatchSettings(Settings* settings);

  // Add a report to the queue.
  bool Add(const std::string& program_name,
           std::map<std::string, fuchsia::mem::Buffer> atttachments,
           std::optional<fuchsia::mem::Buffer> minidump,
           std::map<std::string, std::string> annotations);

  // Processes the pending reports based on the queue's internal state. Returns the number of
  // reports successfully processed.
  //
  // If a report is left as pending, it is not counted as being successfully processed.
  size_t ProcessAll();

  uint64_t Size() const { return pending_reports_.size(); }
  bool IsEmpty() const { return pending_reports_.empty(); }
  bool Contains(const crashpad::UUID& uuid) const;
  const crashpad::UUID& LatestReport() { return pending_reports_.back(); }

 private:
  Queue(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
        std::shared_ptr<InfoContext> info_context, std::unique_ptr<Database> database,
        CrashServer* crash_server);

  // How the queue should handle processing existing pending reports and new reports.
  enum class State {
    Archive,
    Upload,
    LeaveAsPending,
  };

  // Archives all pending reports and clears the queue. Returns the number of reports successfully
  // archived.
  size_t ArchiveAll();

  // Attempts to upload all pending reports and removes the successfully uploaded reports from the
  // queue. Returns the number of reports successfully uploaded.
  size_t UploadAll();

  // Attempts to upload a report.
  //
  // Returns false if the report needs to be processed again.
  bool Upload(const crashpad::UUID& local_report_id);

  // Callback to update |state_| on upload policy changes.
  void OnUploadPolicyChange(const Settings::UploadPolicy& upload_policy);

  // Schedules ProcessAll() to run every hour.
  void ProcessAllEveryHour();

  // Calls ProcessAll() whenever the network becomes reachable.
  void ProcessAllOnNetworkReachable();

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<Database> database_;
  CrashServer* crash_server_;
  QueueInfo info_;

  fuchsia::net::ConnectivityPtr connectivity_;
  // We need to be able to cancel a posted retry task when |this| is destroyed.
  fxl::CancelableClosure network_reconnection_task_;
  backoff::ExponentialBackoff network_reconnection_backoff_;

  State state_ = State::LeaveAsPending;

  std::vector<crashpad::UUID> pending_reports_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Queue);
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_QUEUE_H_
