// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_OUTPUT_PIPELINE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_OUTPUT_PIPELINE_H_

#include <lib/media/cpp/timeline_function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <vector>

#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/clock_reference.h"
#include "src/media/audio/audio_core/effects_stage.h"
#include "src/media/audio/audio_core/mix_stage.h"
#include "src/media/audio/audio_core/pipeline_config.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/stream_usage.h"
#include "src/media/audio/audio_core/volume_curve.h"

namespace media::audio {

class OutputPipeline : public ReadableStream {
 public:
  explicit OutputPipeline(Format format) : ReadableStream(format) {}
  ~OutputPipeline() override = default;

  // Returns the loopback |ReadableStream| for this pipeline.
  virtual std::shared_ptr<ReadableStream> loopback() const = 0;

  // Adds |stream| as an input to be mixed. The given |usage| will indicate where in the pipeline
  // this stream will be routed.
  virtual std::shared_ptr<Mixer> AddInput(
      std::shared_ptr<ReadableStream> stream, const StreamUsage& usage,
      Mixer::Resampler sampler_hint = Mixer::Resampler::Default) = 0;

  // Removes |stream| from the pipeline.
  //
  // It is an error to call |RemoveInput| without exactly one preceeding call to |AddInput| with the
  // same |stream|.
  virtual void RemoveInput(const ReadableStream& stream) = 0;

  // Sets the configuration of all effects with the given instance name.
  virtual fit::result<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config) = 0;
};

class OutputPipelineImpl : public OutputPipeline {
 public:
  // Creates an |OutputPipeline| based on the specification in |config|. The pipeline will
  // ultimately produce output frames via |ReadLock| in the |output_format| requested.
  //
  // |max_block_size_frames| is the largest contiguous region that may be returned from
  // |ReadLock|. If a caller requests a frame region of more that |max_block_size_frames|, then
  // the pipeline will truncate this to only |max_block_size_frames| and the caller will have to
  // call |ReadLock| again to mix the subsequent frames.
  //
  // |ref_clock_to_fractional_frame| is a timeline function that will compute the output frame
  // number (in fixed point format with 13 bits of fractional precision) based on a reference
  // timestamp.
  //
  // The |sampler| is optionally used to select the type of sampler to be used when joining
  // mix stages together.
  OutputPipelineImpl(const PipelineConfig& config, const VolumeCurve& volume_curve,
                     uint32_t max_block_size_frames,
                     TimelineFunction reference_clock_to_fractional_frame, ClockReference ref_clock,
                     Mixer::Resampler sampler = Mixer::Resampler::Default);
  ~OutputPipelineImpl() override = default;

  // |media::audio::OutputPipeline|
  std::shared_ptr<ReadableStream> loopback() const override { return state_.loopback; }
  std::shared_ptr<Mixer> AddInput(
      std::shared_ptr<ReadableStream> stream, const StreamUsage& usage,
      Mixer::Resampler sampler_hint = Mixer::Resampler::Default) override;
  void RemoveInput(const ReadableStream& stream) override;
  fit::result<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config) override;

  // |media::audio::ReadableStream|
  std::optional<ReadableStream::Buffer> ReadLock(zx::time ref_time, int64_t frame,
                                                 uint32_t frame_count) override {
    TRACE_DURATION("audio", "OutputPipeline::ReadLock");
    FX_DCHECK(state_.stream);
    return state_.stream->ReadLock(ref_time, frame, frame_count);
  }
  void Trim(zx::time ref_time) override {
    TRACE_DURATION("audio", "OutputPipeline::Trim");
    FX_CHECK(state_.stream);
    state_.stream->Trim(ref_time);
  }
  TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const override {
    TRACE_DURATION("audio", "OutputPipeline::ReferenceClockToFractionalFrames");
    FX_DCHECK(state_.stream);
    return state_.stream->ReferenceClockToFractionalFrames();
  }
  void SetMinLeadTime(zx::duration min_lead_time) override {
    ReadableStream::SetMinLeadTime(min_lead_time);
    state_.stream->SetMinLeadTime(min_lead_time);
  }
  ClockReference reference_clock() const override { return state_.reference_clock; }

 private:
  struct State {
    State() = default;

    State(const PipelineConfig& config, const VolumeCurve& curve, uint32_t max_block_size_frames,
          TimelineFunction ref_clock_to_fractional_frame, ClockReference ref_clock,
          Mixer::Resampler sampler);

    std::shared_ptr<ReadableStream> CreateMixStage(
        const PipelineConfig::MixGroup& spec, const VolumeCurve& volume_curve, uint32_t block_size,
        fbl::RefPtr<VersionedTimelineFunction> ref_clock_to_output_frame, ClockReference ref_clock,
        uint32_t* usage_mask, Mixer::Resampler sampler);

    std::vector<std::pair<std::shared_ptr<MixStage>, std::vector<StreamUsage>>> mix_stages;
    std::vector<std::shared_ptr<EffectsStage>> effects_stages;
    std::vector<std::pair<std::shared_ptr<ReadableStream>, StreamUsage>> streams;

    // This is the root of the mix graph. The other mix stages must be reachable from this node
    // to actually get mixed.
    std::shared_ptr<ReadableStream> stream;

    std::shared_ptr<ReadableStream> loopback;

    ClockReference reference_clock;
  };

  OutputPipelineImpl(State state);

  MixStage& LookupStageForUsage(const StreamUsage& usage);

  State state_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_OUTPUT_PIPELINE_H_
