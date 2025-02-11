// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_COBALT_METRICS_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_COBALT_METRICS_H_

#include "src/developer/forensics/utils/cobalt/metrics_registry.cb.h"

namespace forensics {
namespace cobalt {

constexpr auto kProjectId = cobalt_registry::kProjectId;

enum class LegacyRebootReason {
  kKernelPanic = cobalt_registry::RebootMetricDimensionReason::KernelPanic,
  kOOM = cobalt_registry::RebootMetricDimensionReason::Oom,
  kCold = cobalt_registry::RebootMetricDimensionReason::Cold,
  kClean = cobalt_registry::RebootMetricDimensionReason::Clean,
  kUnknown = cobalt_registry::RebootMetricDimensionReason::Unknown,
  kHardwareWatchdog = cobalt_registry::RebootMetricDimensionReason::HardwareWatchdog,
  kSoftwareWatchdog = cobalt_registry::RebootMetricDimensionReason::SoftwareWatchdog,
  kBrownout = cobalt_registry::RebootMetricDimensionReason::Brownout,
};

enum class CrashState {
  kUnknown = cobalt_registry::CrashMetricDimensionState::Unknown,
  kFiled = cobalt_registry::CrashMetricDimensionState::Filed,
  kUploaded = cobalt_registry::CrashMetricDimensionState::Uploaded,
  kArchived = cobalt_registry::CrashMetricDimensionState::Archived,
  kGarbageCollected = cobalt_registry::CrashMetricDimensionState::GarbageCollected,
  kDropped = cobalt_registry::CrashMetricDimensionState::Dropped,
};

enum class UploadAttemptState {
  kUnknown = cobalt_registry::CrashUploadAttemptsMetricDimensionState::Unknown,
  kUploadAttempt = cobalt_registry::CrashUploadAttemptsMetricDimensionState::UploadAttempt,
  kUploaded = cobalt_registry::CrashUploadAttemptsMetricDimensionState::Uploaded,
  kArchived = cobalt_registry::CrashUploadAttemptsMetricDimensionState::Archived,
  kGarbageCollected = cobalt_registry::CrashUploadAttemptsMetricDimensionState::GarbageCollected,
};

enum class CrashpadFunctionError {
  kUnknown = cobalt_registry::CrashpadErrorsMetricDimensionFunction::Unknown,
  kInitializeDatabase = cobalt_registry::CrashpadErrorsMetricDimensionFunction::InitializeDatabase,
  kPrepareNewCrashReport =
      cobalt_registry::CrashpadErrorsMetricDimensionFunction::PrepareNewCrashReport,
  kFinishedWritingCrashReport =
      cobalt_registry::CrashpadErrorsMetricDimensionFunction::FinishedWritingCrashReport,
  kGetReportForUploading =
      cobalt_registry::CrashpadErrorsMetricDimensionFunction::GetReportForUploading,
  kRecordUploadComplete =
      cobalt_registry::CrashpadErrorsMetricDimensionFunction::RecordUploadComplete,
  kSkipReportUpload = cobalt_registry::CrashpadErrorsMetricDimensionFunction::SkipReportUpload,
  kLookUpCrashReport = cobalt_registry::CrashpadErrorsMetricDimensionFunction::LookUpCrashReport,
};

enum class TimedOutData {
  kUnknown = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::Unknown,
  kSystemLog = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::SystemLog,
  kKernelLog = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::KernelLog,
  kScreenshot = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::Screenshot,
  kInspect = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::Inspect,
  kChannel = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::Channel,
  kProductInfo = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::ProductInfo,
  kBoardInfo = cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::BoardInfo,
  kLastRebootInfo =
      cobalt_registry::FeedbackDataCollectionTimeoutMetricDimensionData::LastRebootInfo,
};

enum class BugreportGenerationFlow {
  kUnknown = cobalt_registry::BugreportGenerationDurationUsecsMetricDimensionFlow::Unknown,
  kSuccess = cobalt_registry::BugreportGenerationDurationUsecsMetricDimensionFlow::Success,
  kFailure = cobalt_registry::BugreportGenerationDurationUsecsMetricDimensionFlow::Failure,
};

enum class LastRebootReason {
  kUnknown = cobalt_registry::LastRebootUptimeMetricDimensionReason::Unknown,
  kGenericGraceful = cobalt_registry::LastRebootUptimeMetricDimensionReason::GenericGraceful,
  kGenericUngraceful = cobalt_registry::LastRebootUptimeMetricDimensionReason::GenericUngraceful,
  kCold = cobalt_registry::LastRebootUptimeMetricDimensionReason::Cold,
  kBriefPowerLoss = cobalt_registry::LastRebootUptimeMetricDimensionReason::BriefPowerLoss,
  kBrownout = cobalt_registry::LastRebootUptimeMetricDimensionReason::Brownout,
  kKernelPanic = cobalt_registry::LastRebootUptimeMetricDimensionReason::KernelPanic,
  kSystemOutOfMemory = cobalt_registry::LastRebootUptimeMetricDimensionReason::SystemOutOfMemory,
  kHardwareWatchdogTimeout =
      cobalt_registry::LastRebootUptimeMetricDimensionReason::HardwareWatchdogTimeout,
  kSoftwareWatchdogTimeout =
      cobalt_registry::LastRebootUptimeMetricDimensionReason::SoftwareWatchdogTimeout,
  kUserRequest = cobalt_registry::LastRebootUptimeMetricDimensionReason::UserRequest,
  kSystemUpdate = cobalt_registry::LastRebootUptimeMetricDimensionReason::SystemUpdate,
  kHighTemperature = cobalt_registry::LastRebootUptimeMetricDimensionReason::HighTemperature,
  kSessionFailure = cobalt_registry::LastRebootUptimeMetricDimensionReason::SessionFailure,
};

enum class RebootReasonWriteResult {
  kSuccess = cobalt_registry::RebootReasonPersistDurationUsecsMetricDimensionWriteResult::Success,
  kFailure = cobalt_registry::RebootReasonPersistDurationUsecsMetricDimensionWriteResult::Failure,
};

inline constexpr uint32_t MetricIDForEventCode(const RebootReasonWriteResult write_result) {
  return cobalt_registry::kRebootReasonPersistDurationUsecsMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const LastRebootReason bug_report) {
  return cobalt_registry::kLastRebootUptimeMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const BugreportGenerationFlow bug_report) {
  return cobalt_registry::kBugreportGenerationDurationUsecsMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const TimedOutData data) {
  return cobalt_registry::kFeedbackDataCollectionTimeoutMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const LegacyRebootReason reason) {
  return cobalt_registry::kRebootMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const CrashState state) {
  return cobalt_registry::kCrashMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const UploadAttemptState state) {
  return cobalt_registry::kCrashUploadAttemptsMetricId;
}

inline constexpr uint32_t MetricIDForEventCode(const CrashpadFunctionError function) {
  return cobalt_registry::kCrashpadErrorsMetricId;
}

namespace internal {

// Determines if all passed event code types correspond to the same metric ids.
//
// The base case needs to be provided with a default value to return.
template <typename EventCodeTypeDefault, typename... EventCodeTypes>
struct MetricIDChecker {
  static constexpr uint32_t metric_id = MetricIDForEventCode(static_cast<EventCodeTypeDefault>(0));
  static constexpr bool all_same = true;
};

// Uses the first event code type as the default for the base case and check if all of the metric
// ids for the event codes in the parameter pack are the same.
template <typename EventCodeTypeDefault, typename EventCodeTypesH, typename... EventCodeTypesT>
struct MetricIDChecker<EventCodeTypeDefault, EventCodeTypesH, EventCodeTypesT...> {
  static constexpr uint32_t metric_id = MetricIDForEventCode(static_cast<EventCodeTypesH>(0));
  static constexpr bool all_same =
      metric_id == MetricIDChecker<EventCodeTypeDefault, EventCodeTypesT...>::metric_id;
};

}  // namespace internal

template <typename EventCodeTypesH, typename... EventCodeTypesT>
inline constexpr uint32_t MetricIDForEventCode(const EventCodeTypesH event_code,
                                               const EventCodeTypesT... event_codes_t) {
  constexpr internal::MetricIDChecker<EventCodeTypesH, EventCodeTypesH, EventCodeTypesT...> checker;
  static_assert(checker.all_same, "All event codes need to have the same metric id");
  return checker.metric_id;
}

enum class EventType {
  kOccurrence,
  kCount,
  kTimeElapsed,
  kMultidimensionalOccurrence,
};

inline constexpr EventType EventTypeForEventCode(const RebootReasonWriteResult write_result) {
  return EventType::kTimeElapsed;
}

inline constexpr EventType EventTypeForEventCode(const LastRebootReason status) {
  return EventType::kTimeElapsed;
}

inline constexpr EventType EventTypeForEventCode(const BugreportGenerationFlow status) {
  return EventType::kTimeElapsed;
}

inline constexpr EventType EventTypeForEventCode(const TimedOutData data) {
  return EventType::kOccurrence;
}

inline constexpr EventType EventTypeForEventCode(const LegacyRebootReason reason) {
  return EventType::kOccurrence;
}

inline constexpr EventType EventTypeForEventCode(const CrashState state) {
  return EventType::kOccurrence;
}

inline constexpr EventType EventTypeForEventCode(const UploadAttemptState state) {
  return EventType::kCount;
}

inline constexpr EventType EventTypeForEventCode(const CrashpadFunctionError function) {
  return EventType::kOccurrence;
}

}  // namespace cobalt
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_COBALT_METRICS_H_
