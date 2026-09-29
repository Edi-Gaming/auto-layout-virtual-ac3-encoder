// OhlMusicUpmixer.h — stereo -> discrete 5.1 OHL Music spatializer.
//
// v0.4 keeps the v0.3 ambience-first topology and exposes the tuning dimensions that were
// previously hard-coded: ambience band emphasis, steering time constants, transient detector
// sensitivity/recovery, rear voicing, rear trims, and a center sparkle band-pass.
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

    // Base gain applied only to stereo-difference residue.
    float widthFloor = 0.16f;

    // Multiband ambience analysis weighting. Values do not have to sum to 1.0; the resulting
    // ambience score is clamped after the weighted sum.
    float ambienceLowWeight = 0.08f;
    float ambienceMidWeight = 0.46f;
    float ambienceHighWeight = 0.46f;

    // Block-level steering time constants. v0.3's 0.28 / 0.06 per-AC3-frame smoothing is
    // approximately 100 ms attack / 520 ms release at 48 kHz, so these defaults preserve it.
    float ambienceAttackMs = 100.0f;
    float ambienceReleaseMs = 520.0f;

    // Sample-local direct-event rejection. Higher reject keeps onsets forward.
    float directReject = 0.78f;

    // Fast/slow envelope ratio at which onset rejection begins. Higher threshold = less
    // sensitive. directRecoveryMs controls how quickly the fast detector lets the rear recover.
    float directThreshold = 1.45f;
    float directRecoveryMs = 18.0f;

    // Gain for centered band-limited treble sent to the physical center.
    float centerTrebleGain = 0.18f;
    float centerTrebleHz = 2400.0f;
    float centerLowpassHz = 16000.0f;

    // Rear voicing / balance.
    float rearHighpassHz = 160.0f;
    float rearLowpassHz = 18000.0f;
    float rearLeftTrim = 1.0f;
    float rearRightTrim = 1.0f;

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
  OnePoleLowpass rearLpL_;
  OnePoleLowpass rearLpR_;
  OnePoleHighpass centerHp_;
  OnePoleLowpass centerLp_;

  EnvelopeFollower eventFast_;
  EnvelopeFollower eventSlow_;

  bool gainInitialized_ = false;
  float surroundAmount_ = 0.0f;
  float lastCorrelation_ = 1.0f;
};
