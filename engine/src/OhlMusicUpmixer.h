// OhlMusicUpmixer.h — stereo -> discrete 5.1 OHL Music spatializer.
//
// v0.6 makes vocal anchoring structural instead of classifier-dependent:
//  * exact shared same-polarity L/R content is removed from the rear source,
//  * the remaining independent L/R residuals are split into low / vocal-body / air bands,
//  * Front Lock directly attenuates the vocal-body band while leaving high-frequency ambience and
//    asymmetric effects available behind the listener.
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

    // 0..1 attenuation of the residual vocal/body band in the rears after exact common-content
    // subtraction. 0 = full residual midband; 1 = remove that band from the rear field.
    float frontLock = 0.88f;
    float frontLockLowHz = 250.0f;
    float frontLockHighHz = 5200.0f;

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
  std::array<OnePoleLowpass, 4> rearVoiceLowSplitL_;
  std::array<OnePoleLowpass, 4> rearVoiceLowSplitR_;
  std::array<OnePoleLowpass, 4> rearVoiceHighSplitL_;
  std::array<OnePoleLowpass, 4> rearVoiceHighSplitR_;
  OnePoleHighpass centerHp_;
  OnePoleLowpass centerLp_;

  EnvelopeFollower eventFast_;
  EnvelopeFollower eventSlow_;

  bool gainInitialized_ = false;
  float surroundAmount_ = 0.0f;
  float lastCorrelation_ = 1.0f;
  float lastFrontLockConfidence_ = 0.0f;
};
