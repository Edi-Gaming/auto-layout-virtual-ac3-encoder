// OhlMusicUpmixer.h — stereo -> discrete 5.1 OHL Music spatializer.
//
// v0.3 stops synthesizing width from same-side program copies. Rear energy comes from
// stereo-difference / diffuse residue only, with multiband block analysis and sample-local
// direct-event rejection so claps/snare attacks stay in front while ambience/tails survive.
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

    // Maximum adaptive ambience contribution above the base side-width feed.
    float surroundGain = 0.70f;

    // Base gain applied only to stereo-difference residue. Unlike v0.2 this never feeds common
    // L/R program material to the rear speakers.
    float widthFloor = 0.16f;

    // 0..1 strength of sample-local direct-event rejection. Higher values keep clap/snare attacks
    // forward while allowing their following stereo ambience/tails back into the surrounds.
    float directReject = 0.78f;

    // Gain for centered, high-passed content sent to the physical center.
    float centerTrebleGain = 0.18f;
    float centerTrebleHz = 2400.0f;

    // Rear channels are high-passed so bass stays anchored to FL/FR.
    float rearHighpassHz = 160.0f;

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

  struct OnePoleLowpass
  {
    void Configure(float hz, int sampleRate);
    void Reset();
    float Process(float x);

    float alpha = 1.0f;
    float y = 0.0f;
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

  struct EnvelopeFollower
  {
    void Configure(float attackMs, float releaseMs, int sampleRate);
    void Reset();
    float Process(float x);

    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float value = 0.0f;
  };

  Params params_{};
  std::array<int, kChannels> delaySamples_{{0, 0, 0, 0, 0, 0}};
  std::array<DelayLine, kChannels> delays_;

  OnePoleLowpass analysisLowL_;
  OnePoleLowpass analysisLowR_;
  OnePoleLowpass analysisMidL_;
  OnePoleLowpass analysisMidR_;

  OnePoleHighpass rearHpL_;
  OnePoleHighpass rearHpR_;
  OnePoleHighpass centerHp_;

  EnvelopeFollower eventFast_;
  EnvelopeFollower eventSlow_;

  bool gainInitialized_ = false;
  float surroundAmount_ = 0.0f;
  float lastCorrelation_ = 1.0f;
};
