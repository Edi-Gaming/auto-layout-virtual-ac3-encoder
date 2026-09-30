// OhlMusicUpmixer.h — stereo -> discrete 5.1 OHL Music spatializer.
//
// v0.7 is a sparse ambience extractor, not a second pair of mains:
//  * rear source starts at zero and receives only unshared L/R residual information,
//  * a diffuse-content gate opens adaptive ambience while direct/panned program stays mostly front,
//  * a hard rear-energy budget prevents the surround pair from becoming a duplicate stereo pair.
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

    float surroundGain = 0.70f;
    float widthFloor = 0.16f;

    float ambienceLowWeight = 0.08f;
    float ambienceMidWeight = 0.46f;
    float ambienceHighWeight = 0.46f;
    float ambienceAttackMs = 100.0f;
    float ambienceReleaseMs = 520.0f;
    float diffuseThreshold = 0.18f;

    // 0..1 attenuation strength applied to unshared residuals when the packet is strongly
    // center/coherent. Shared content itself is always removed structurally.
    float frontLock = 0.88f;

    // Maximum average rear-channel RMS as a fraction of average front-channel RMS.
    // This is the final safety rail against "rear mains".
    float rearBudget = 0.16f;

    float directReject = 0.78f;
    float directThreshold = 1.45f;
    float directRecoveryMs = 18.0f;

    float centerTrebleGain = 0.18f;
    float centerTrebleHz = 2400.0f;
    float centerLowpassHz = 16000.0f;

    float rearHighpassHz = 160.0f;
    float rearLowpassHz = 18000.0f;
    float rearLeftTrim = 1.0f;
    float rearRightTrim = 1.0f;

    std::array<float, kChannels> distanceInches{{33.0f, 33.0f, 30.0f, 33.0f, 27.0f, 33.0f}};
  };

  bool Init(const Params& params);
  void Reset();

  void ProcessStereo(const float* stereo, size_t frames, float* out51);

  float LastCorrelation() const { return lastCorrelation_; }
  float LastSurroundAmount() const { return surroundAmount_; }
  float LastFrontLockConfidence() const { return lastFrontLockConfidence_; }
  int ProcessingLatencySamples() const { return 0; }
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
  OnePoleLowpass rearLpL_;
  OnePoleLowpass rearLpR_;
  std::vector<float> rearScratchL_;
  std::vector<float> rearScratchR_;
  OnePoleHighpass centerHp_;
  OnePoleLowpass centerLp_;

  EnvelopeFollower eventFast_;
  EnvelopeFollower eventSlow_;

  bool gainInitialized_ = false;
  float surroundAmount_ = 0.0f;
  float lastCorrelation_ = 1.0f;
  float lastFrontLockConfidence_ = 0.0f;
};
