// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_BASE_RENDERER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_BASE_RENDERER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/zx/clock.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/clock_reference.h"
#include "src/media/audio/audio_core/context.h"
#include "src/media/audio/audio_core/link_matrix.h"
#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/route_graph.h"
#include "src/media/audio/audio_core/usage_settings.h"
#include "src/media/audio/audio_core/utils.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/wav/wav_writer.h"

namespace media::audio {

constexpr bool kEnableRendererWavWriters = false;

class AudioAdmin;
class StreamRegistry;

class BaseRenderer : public AudioObject,
                     public fuchsia::media::AudioRenderer,
                     public std::enable_shared_from_this<BaseRenderer> {
 public:
  ~BaseRenderer() override;

  void OnRenderRange(int64_t presentation_time, uint32_t duration){};

  // |fuchsia::media::AudioRenderer|
  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) final;
  void RemovePayloadBuffer(uint32_t id) final;
  void SetPtsUnits(uint32_t tick_per_second_numerator, uint32_t tick_per_second_denominator) final;
  void SetPtsContinuityThreshold(float threshold_seconds) final;
  void GetReferenceClock(GetReferenceClockCallback callback) final;
  void SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) final;
  void SendPacketNoReply(fuchsia::media::StreamPacket packet) final;
  void EndOfStream() final;
  void DiscardAllPackets(DiscardAllPacketsCallback callback) final;
  void DiscardAllPacketsNoReply() final;
  void Play(int64_t reference_time, int64_t media_time, PlayCallback callback) final;
  void PlayNoReply(int64_t reference_time, int64_t media_time) final;
  void Pause(PauseCallback callback) final;
  void PauseNoReply() final;
  void EnableMinLeadTimeEvents(bool enabled) final;
  void GetMinLeadTime(GetMinLeadTimeCallback callback) final;

  ClockReference reference_clock() const { return reference_clock_ref_; }

 protected:
  BaseRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request,
               Context* context);

  Context& context() const { return context_; }

  // |media::audio::AudioObject|
  void OnLinkAdded() override;
  fit::result<std::shared_ptr<ReadableStream>, zx_status_t> InitializeDestLink(
      const AudioObject& dest) override;
  void CleanupDestLink(const AudioObject& dest) override;

  virtual void ReportStart() {}
  virtual void ReportStop() {}
  virtual void Shutdown();

  // Hook called when the minimum clock lead time requirement changes.
  void ReportNewMinLeadTime();

  bool IsOperating();

  void InvalidateConfiguration() { config_validated_ = false; }

  float stream_gain_db_ = 0.0;

  // Minimum Lead Time state
  zx::duration min_lead_time_;

  const zx::clock& optimal_clock() { return optimal_clock_; }
  void set_optimal_clock(zx::clock optimal_clock) { optimal_clock_ = std::move(optimal_clock); }
  void set_reference_clock(zx::clock ref_clock) { reference_clock_ = std::move(ref_clock); }

  fidl::Binding<fuchsia::media::AudioRenderer>& binding() { return audio_renderer_binding_; }

 private:
  // Recompute the minimum clock lead time based on the current set of outputs
  // we are linked to.  If this requirement is different from the previous
  // requirement, report it to our users (if they care).
  void RecomputeMinLeadTime();

  bool ValidateConfig();
  void ComputePtsToFracFrames(int64_t first_pts);

  Context& context_;
  fidl::Binding<fuchsia::media::AudioRenderer> audio_renderer_binding_;

  std::unordered_map<uint32_t, fbl::RefPtr<RefCountedVmoMapper>> payload_buffers_;
  bool config_validated_ = false;

  void CreateOptimalReferenceClock();
  void EstablishDefaultReferenceClock();

  // PTS interpolation state.
  FractionalFrames<int64_t> next_frac_frame_pts_{0};
  TimelineRate pts_ticks_per_second_;
  TimelineRate frac_frames_per_pts_tick_;
  TimelineFunction pts_to_frac_frames_;
  bool pts_to_frac_frames_valid_ = false;
  float pts_continuity_threshold_ = 0.0f;
  bool pts_continuity_threshold_set_ = false;
  FractionalFrames<int64_t> pts_continuity_threshold_frac_frame_{0};

  // Play/Pause state
  FractionalFrames<int64_t> pause_time_frac_frames_;
  bool pause_time_frac_frames_valid_ = false;
  TimelineRate frac_frames_per_ref_tick_;

  // Minimum Clock Lead Time state
  bool min_lead_time_events_enabled_ = false;

  fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames_;

  std::unordered_map<const AudioObject*, std::shared_ptr<PacketQueue>> packet_queues_;
  Packet::Allocator packet_allocator_;

  WavWriter<kEnableRendererWavWriters> wav_writer_;

  // This clock is created and tuned by audio_core
  zx::clock optimal_clock_;

  // Whether default, optimal or custom clock, audio_core will treat this as not-rate-adjustable
  // (although if set to the optimal_clock_, tuning of that clock will be reflected here)
  zx::clock reference_clock_;
  ClockReference reference_clock_ref_ = ClockReference::MakeReadonly(reference_clock_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_BASE_RENDERER_H_
