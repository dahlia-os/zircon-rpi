// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DATABASE_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DATABASE_H_

#include <fuchsia/mem/cpp/fidl.h>

#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>

#include "src/developer/forensics/crash_reports/info/database_info.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/upload_report.h"
#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/util/misc/uuid.h"

namespace forensics {
namespace crash_reports {

extern const uint64_t kCrashpadDatabaseMaxSizeInKb;

// Wrapper around the Crashpad database that also stores annotations.
class Database {
 public:
  static std::unique_ptr<Database> TryCreate(
      std::shared_ptr<InfoContext> info_context,
      uint64_t max_crashpad_database_size_in_kb = kCrashpadDatabaseMaxSizeInKb);

  // Make a new report in |database_|.
  //
  // Return false if there is an error with the database.
  bool MakeNewReport(const std::map<std::string, fuchsia::mem::Buffer>& attachments,
                     const std::optional<fuchsia::mem::Buffer>& minidump,
                     const std::map<std::string, std::string>& annotations,
                     crashpad::UUID* local_report_id);

  // Construct and return the |UploadReport| for |local_report_id|.
  //
  // If there's an error with |database_| return nullptr.
  std::unique_ptr<UploadReport> GetUploadReport(const crashpad::UUID& local_report_id);

  void IncrementUploadAttempt(const crashpad::UUID& local_report_id);

  // Record |upload_report| as uploaded and clean up the report's annotations
  //
  // Return false if there is an error with |database_|.
  bool MarkAsUploaded(std::unique_ptr<UploadReport> upload_report,
                      const std::string& server_report_id);

  // Record |local_report_id| as skipped in |database_| and clean up the report's annotations
  //
  // Return false if there is an error with |database_|.
  bool Archive(const crashpad::UUID& local_report_id);

  // Deletes oldest (determined by creation_time) crash reports to keep |database_| under a maximum
  // size and removes expired lockfiles, metadata without report files, report files without
  // metadata from |database_|, and orphaned attachments. Removes all data from |additional_data_|
  // that is not in |database_|.
  //
  // Return the number of reports that are removed from |database_|.
  size_t GarbageCollect();

  uint64_t MaxCrashpadDatabaseSizeInKb() { return max_crashpad_database_size_in_kb_; }

  ~Database() = default;

 private:
  bool Contains(const crashpad::UUID& local_report_id);
  // Allows for crashpad::UUID to be used as the key in an |std::unordered_map|.
  struct UUIDHasher {
    size_t operator()(const crashpad::UUID& uuid) const {
      return std::hash<std::string>()(uuid.ToString());
    }
  };

  // Data pertinent to a crash report not stored in |database_|.
  struct AdditionalData {
    bool has_minidump;
    uint64_t upload_attempts;
    std::map<std::string, std::string> annotations;
  };

  Database(std::unique_ptr<crashpad::CrashReportDatabase> database,
           uint64_t max_crashpad_database_size_in_kb, std::shared_ptr<InfoContext> info_context);

  // Removes |local_report_id| from |additional_data_|.
  void CleanUp(const crashpad::UUID& local_report_id);

  std::unique_ptr<crashpad::CrashReportDatabase> database_;
  const uint64_t max_crashpad_database_size_in_kb_;
  DatabaseInfo info_;
  std::unordered_map<crashpad::UUID, AdditionalData, UUIDHasher> additional_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Database);
};

}  // namespace crash_reports
}  // namespace forensics
#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DATABASE_H_
