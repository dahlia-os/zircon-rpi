// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_SYSTEM_METRICS_DAEMON_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_SYSTEM_METRICS_DAEMON_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/inspect/cpp/component.h>

#include <chrono>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "src/cobalt/bin/system-metrics/activity_listener.h"
#include "src/cobalt/bin/system-metrics/archivist_stats_fetcher.h"
#include "src/cobalt/bin/system-metrics/cpu_stats_fetcher.h"
#include "src/cobalt/bin/system-metrics/log_stats_fetcher.h"
#include "src/cobalt/bin/system-metrics/metrics_registry.cb.h"
#include "src/cobalt/bin/system-metrics/temperature_fetcher.h"
#include "src/cobalt/bin/utils/clock.h"
#include "third_party/cobalt/src/registry/buckets_config.h"

// A daemon to send system metrics to Cobalt.
//
// Usage:
//
// async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
// std::unique_ptr<sys::ComponentContext> context(
//     sys::ComponentContext::CreateAndServeOutgoingDirectory());
// SystemMetricsDaemon daemon(loop.dispatcher(), context.get());
// daemon.StartLogging();
// loop.Run();
class SystemMetricsDaemon {
 public:
  // Constructor
  //
  // |dispatcher|. This is used to schedule future work.
  //
  // |context|. The Cobalt LoggerFactory interface is fetched from this context.
  SystemMetricsDaemon(async_dispatcher_t* dispatcher, sys::ComponentContext* context);

  // Starts asynchronously logging all system metrics.
  void StartLogging();

  // Reader side must use the exact name to read from Inspect.
  // Design doc in go/fuchsia-metrics-to-inspect-design.
  // Details about config file are in b/152076901#comment6.
  static constexpr const char* kInspecPlatformtNodeName = "platform_metrics";

  static constexpr const char* kTemperatureNodeName = "temperature";
  static constexpr const char* kReadingTemperature = "readings";
  static constexpr size_t kTempArraySize = 6;

  // Details about config file are in b/152073842#comment6.
  static constexpr const char* kCPUNodeName = "cpu";
  static constexpr const char* kReadingCPUMax = "max";
  static constexpr const char* kReadingCPUMean = "mean";
  static constexpr size_t kCPUArraySize = 6;

 private:
  friend class SystemMetricsDaemonTest;
  friend class SystemMetricsDaemonInitializationTest;

  // This private constructor is intended for use in tests. |context| may
  // be null because InitializeLogger() will not be invoked. Instead,
  // pass a non-null |logger| which may be a local mock that does not use FIDL.
  SystemMetricsDaemon(async_dispatcher_t* dispatcher, sys::ComponentContext* context,
                      fuchsia::cobalt::Logger_Sync* logger,
                      fuchsia::cobalt::Logger_Sync* component_diagnostics_logger,
                      std::unique_ptr<cobalt::SteadyClock> clock,
                      std::unique_ptr<cobalt::CpuStatsFetcher> cpu_stats_fetcher,
                      std::unique_ptr<cobalt::TemperatureFetcher> temperature_fetcher,
                      std::unique_ptr<cobalt::LogStatsFetcher> log_stats_fetcher,
                      std::unique_ptr<cobalt::ActivityListener> activity_listener,
                      std::unique_ptr<cobalt::ArchivistStatsFetcher> archivist_stats_fetcher);

  void InitializeLogger();
  void InitializeDiagnosticsLogger();

  void InitializeRootResourceHandle();

  // If the peer has closed the FIDL connection, automatically reconnect.
  zx_status_t ReinitializeIfPeerClosed(zx_status_t zx_status);
  zx_status_t ReinitializeDiagnosticsIfPeerClosed(zx_status_t zx_status);

  // Calls LogUpPingAndLifeTimeEvents,
  // and then uses the |dispatcher| passed to the constructor to
  // schedule the next round.
  void RepeatedlyLogUpPingAndLifeTimeEvents();

  // Calls LogFuchsiaUptime and then uses the |dispatcher| passed to the
  // constructor to schedule the next round.
  void RepeatedlyLogUptime();

  // Calls LogCpuUsage,
  // then uses the |dispatcher| passed to the constructor to schedule
  // the next round.
  void RepeatedlyLogCpuUsage();

  // Calls LogLogStats and then uses the |dispatcher| passed to the
  // constructor to schedule the next round.
  void RepeatedlyLogLogStats();

  // Calls LogArchivistStats and then uses the |dispatcher| passed to the
  // constructor to schedule the next round.
  void RepeatedlyLogArchivistStats();

  // Check if fetching device temperature is supported, and if successful
  // start logging temperature.
  // If it fails, attempt again after 1 minute. Repeat the process
  // |remaining_attempts| times.
  void LogTemperatureIfSupported(int remaining_attempts);

  // Create linear bucket config with the bucket_floor, number of buckets and step size.
  std::unique_ptr<cobalt::config::IntegerBucketConfig> InitializeLinearBucketConfig(
      int64_t bucket_floor, int32_t num_buckets, int32_t step_size);

  // Calls LogTemperature,
  // then uses the |dispatcher| passed to the constructor to schedule
  // the next round.
  void RepeatedlyLogTemperature();

  // Returns the amount of time since SystemMetricsDaemon started.
  std::chrono::seconds GetUpTime();

  // Calls LogFuchsiaUpPing and LogFuchsiaLifetimeEvents.
  //
  // Returns the amount of time before this method needs to be invoked again.
  std::chrono::seconds LogUpPingAndLifeTimeEvents();

  // Logs one or more UpPing events depending on how long the device has been
  // up.
  //
  // |uptime| An estimate of how long since device boot time.
  //
  // First the "Up" event is logged indicating only that the device is up.
  //
  // If the device has been up for at least a minute then "UpOneMinute" is also
  // logged.
  //
  // If the device has been up for at least 10 minutes, then "UpTenMinutes" is
  // also logged. Etc.
  //
  // Returns the amount of time before this method needs to be invoked again.
  std::chrono::seconds LogFuchsiaUpPing(std::chrono::seconds uptime);

  // Logs one FuchsiaLifetimeEvent event of type "Boot" the first time it
  // is invoked and does nothing on subsequent invocations.
  //
  // Returns the amount of time before this method needs to be invoked again.
  // Currently returns std::chrono::seconds::max().
  std::chrono::seconds LogFuchsiaLifetimeEvents();

  // Once per hour, rounds the current uptime down to the nearest number of
  // hours and logs an event for the fuchsia_uptime metric.
  //
  // Returns the amount of time before this method needs to be invoked again.
  // This is the number of seconds until the uptime reaches the next full hour.
  std::chrono::seconds LogFuchsiaUptime();

  // Fetches and logs system-wide CPU usage.
  //
  // Returns the amount of time before this method needs to be invoked again.
  std::chrono::seconds LogCpuUsage();

  // Fetches and logs the number of error log messages across all components.
  void LogLogStats();

  // Fetches and logs archivist stats.
  //
  // Returns the amount of time before this method needs to be invoked again.
  std::chrono::seconds LogArchivistStats();

  // Helper function to store the fetched CPU data and store until flush.
  void StoreCpuData(double cpu_percentage);  // histogram, flush every 10 min

  // Helper function to call Cobalt logger's LogCobaltEvent to log
  // cpu percentages.
  bool LogCpuToCobalt();  // INT_HISTOGRAM metric type

  // Fetches and logs device temperature.
  //
  // Returns the amount of time before this method needs to be invoked again.
  std::chrono::seconds LogTemperature();

  // Helper function to call Cobalt logger's LogIntHistogram to log
  // a vector of temperature readings taken in one minute into Cobalt.
  void LogTemperatureToCobalt();

  // Callback function to be called by ActivityListener to update current_state_
  void UpdateState(fuchsia::ui::activity::State state) { current_state_ = state; }

  bool boot_reported_ = false;
  async_dispatcher_t* const dispatcher_;
  sys::ComponentContext* context_;
  fuchsia::cobalt::LoggerFactorySyncPtr factory_;
  fuchsia::cobalt::LoggerSyncPtr logger_fidl_proxy_;
  fuchsia::cobalt::Logger_Sync* logger_;
  fuchsia::cobalt::LoggerFactorySyncPtr component_diagnostics_factory_;
  fuchsia::cobalt::LoggerSyncPtr component_diagnostics_logger_fidl_proxy_;
  fuchsia::cobalt::Logger_Sync* component_diagnostics_logger_;
  std::chrono::steady_clock::time_point start_time_;
  std::unique_ptr<cobalt::SteadyClock> clock_;
  std::unique_ptr<cobalt::CpuStatsFetcher> cpu_stats_fetcher_;
  std::unique_ptr<cobalt::TemperatureFetcher> temperature_fetcher_;
  std::unique_ptr<cobalt::LogStatsFetcher> log_stats_fetcher_;
  std::unique_ptr<cobalt::ActivityListener> activity_listener_;
  std::unique_ptr<cobalt::ArchivistStatsFetcher> archivist_stats_fetcher_;
  fuchsia::ui::activity::State current_state_ = fuchsia::ui::activity::State::UNKNOWN;
  fidl::InterfacePtr<fuchsia::ui::activity::Provider> activity_provider_;

  sys::ComponentInspector inspector_;
  inspect::Node platform_metric_node_;

  inspect::Node metric_cpu_node_;
  inspect::DoubleArray inspect_cpu_max_;
  inspect::DoubleArray inspect_cpu_mean_;
  double cpu_usage_accumulator_ = 0;
  double cpu_usage_max_ = 0;
  size_t cpu_array_index_ = 0;

  inspect::Node metric_temperature_node_;
  inspect::IntArray inspect_temperature_readings_;

  template <typename T>
  T GetCobaltEventCodeForDeviceState(fuchsia::ui::activity::State state) {
    switch (state) {
      case fuchsia::ui::activity::State::IDLE:
        return T::Idle;
      case fuchsia::ui::activity::State::ACTIVE:
        return T::Active;
      case fuchsia::ui::activity::State::UNKNOWN:
        return T::Unknown;
    }
  }
  struct CpuWithActivityState {
    double cpu_percentage;
    fuchsia::ui::activity::State state;
  };
  std::unordered_map<fuchsia::ui::activity::State, std::unordered_map<uint32_t, uint32_t>>
      activity_state_to_cpu_map_;
  std::unordered_map<uint32_t, uint32_t> temperature_map_;
  uint32_t num_temps_ = 0;
  uint32_t cpu_data_stored_ = 0;
  // This bucket config is used to calculate the histogram bucket index for a given cpu percentage.
  // Usage: cpu_bucket_config_->BucketIndex(cpu_percentage * 100)
  std::unique_ptr<cobalt::config::IntegerBucketConfig> cpu_bucket_config_;
  // This bucket config is used to calculate the histogram bucket index for a given temperature.
  // Usage: temperature_bucket_config_->BucketIndex(temperature)
  std::unique_ptr<cobalt::config::IntegerBucketConfig> temperature_bucket_config_;

 protected:
  // This function should only be used in test to change temperature fetcher.
  void SetTemperatureFetcher(std::unique_ptr<cobalt::TemperatureFetcher> fetcher);
};

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_SYSTEM_METRICS_DAEMON_H_
