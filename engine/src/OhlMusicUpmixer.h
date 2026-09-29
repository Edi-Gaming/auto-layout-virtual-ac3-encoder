// OhlMusicUpmixer.h — stereo -> discrete 5.1 OHL Music spatializer.
//
// v0.2 keeps the original L/R program in FL/FR, adds a permanent low-level decorrelated width
// bed so ordinary modern stereo never collapses to "fronts only", layers correlation-aware
// ambience extraction on top, and optionally feeds only centered high-frequency information to
// the physical center. LFE remains silent.
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

    // Maximum adaptive L-R ambience contribution.
    float surroundGain = 0.78f;

    // Always-present same-side width bed after center suppression + decorrelation.
    // This is deliberately independent of the block analyser so transients cannot gate it off.
    float widthFloor = 0.22f;

    // Gain for centered, high-passed content sent to the physical center. Intended to use the
    // Bose cube only for "sparkle"/intelligibility rather than making it carry the whole vocal.
    float centerTrebleGain = 0.18f;
    float centerTrebleHz = 2400.0f;

    // Rear channels are high-passed so bass stays anchored to FL/FR.
    float rearHighpassHz = 140.0f;

    // Listening-position distances in inches, channel order FL FR FC LFE SL SR.
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

  struct OnePoleHighpass
  {
    void Configure(float hz, int sampleRate);
    void Reset();
    float Process(float x);

    float alpha = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
  };

  struct OnePoleAllpass
  {
    void Configure(float coefficient);
    void Reset();
    float Process(float x);

    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
  };

  Params params_{};
  std::array<int, kChannels> delaySamples_{{0, 0, 0, 0, 0, 0}};
  std::array<DelayLine, kChannels> delays_;

  OnePoleHighpass rearHpL_;
  OnePoleHighpass rearHpR_;
  OnePoleHighpass centerHp_;
  OnePoleAllpass rearApL_;
  OnePoleAllpass rearApR_;

  bool gainInitialized_ = false;
  float surroundAmount_ = 0.0f;
  float lastCorrelation_ = 1.0f;
};
