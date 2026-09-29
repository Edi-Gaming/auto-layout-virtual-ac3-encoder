// OhlMusicUpmixer.h — conservative stereo -> discrete 5.1 music spatializer.
//
// OHL Music is intentionally separate from FFmpeg's generic surround filter and from the
// hardware-validated native/auto AC3 paths. It preserves the original L/R program in FL/FR,
// keeps C and LFE silent for a phantom-center-first presentation, and derives a conservative
// anti-correlated surround bed from stereo difference information.
//
// Output channel order matches Windows/AC3 5.1-back: FL FR FC LFE BL BR.
#pragma once

#include <array>
#include <cstddef>
#include <vector>

class OhlMusicUpmixer
{
public:
  static constexpr int kChannels = 6;

  struct Params
  {
    int sampleRate = 48000;

    // 0..1-ish scalar applied to extracted spatial information. 0.55 is intentionally a little
    // more enveloping than the receiver's current PLII Music presentation without making the
    // surrounds compete with the fronts.
    float surroundGain = 0.55f;

    // Listening-position distances in inches, channel order FL FR FC LFE SL SR.
    // Defaults are Edi's 2026-09-29 measurements. Delay is added to nearer speakers so all
    // direct channels are referenced to the farthest measured speaker.
    std::array<float, kChannels> distanceInches{{33.0f, 33.0f, 30.0f, 33.0f, 27.0f, 33.0f}};
  };

  bool Init(const Params& params);
  void Reset();

  // stereo: interleaved L,R float PCM, frames sample frames.
  // out51 : interleaved FL,FR,FC,LFE,SL,SR float PCM, frames sample frames.
  void ProcessStereo(const float* stereo, size_t frames, float* out51);

  float LastCorrelation() const { return lastCorrelation_; }
  float LastSurroundAmount() const { return surroundAmount_; }
  const std::array<int, kChannels>& DelaySamples() const { return delaySamples_; }

private:
  struct DelayLine
  {
    void Configure(int samples);
    void Reset();
    float Process(float x);

    std::vector<float> buffer;
    size_t pos = 0;
  };

  Params params_{};
  std::array<int, kChannels> delaySamples_{{0, 0, 0, 0, 0, 0}};
  std::array<DelayLine, kChannels> delays_;

  bool gainInitialized_ = false;
  float surroundAmount_ = 0.0f;
  float lastCorrelation_ = 1.0f;
};
