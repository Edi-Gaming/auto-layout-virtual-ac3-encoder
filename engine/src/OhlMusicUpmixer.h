// OhlMusicUpmixer.h — stereo -> discrete 5.1 OHL Music spatializer.
//
// v0.9 removes the nonlinear sample-wise rear extractor entirely:
//  * exact mono/center disappears naturally through the linear M/S side signal,
//  * two short all-pass phase networks decorrelate SL/SR without adding nonlinear distortion,
//  * front-lock, transient protection and rear budget act only as slow gains.
//
// Output channel order matches Windows/AC3 5.1-back: FL FR FC LFE BL BR.
#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "OhlSpatialAnalyzer.h"
#include "OhlSpectralRouter.h"

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
    float diffuseThreshold = 0.10f;

    // v0.10 spectral intelligence. 0 = legacy broad-band v0.9 recognition, 1 = STFT classifier.
    float spectralIntelligence = 0.90f;
    float spatialBinThreshold = 0.30f;

    // v0.11 renderer controls.
    float perBinRouting = 0.55f;
    float spectralAcquireMs = 65.0f;
    float spectralReleaseMs = 520.0f;
    float dimension = 0.0f;     // -1 front, +1 rear
    float centerWidth = 1.0f;   // 1 = phantom center untouched, 0 = strongest physical-center focus

    std::array<float, 4> spectralSteering{{0.18f, 0.55f, 0.90f, 1.10f}};
    std::array<float, 4> spectralFrontLock{{0.30f, 1.00f, 0.82f, 0.25f}};

    // 0..1 attenuation strength applied to the rear side field when a coherent front center
    // dominates. This is a gain control only; it never modifies waveform shape sample-by-sample.
    float frontLock = 0.88f;

    // Maximum average rear-channel RMS as a fraction of average front-channel RMS.
    // This is the final safety rail against "rear mains".
    float rearBudget = 0.22f;

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
  float LastSpectralAmbience() const { return lastSpectralAmbience_; }
  float LastSpatialBinFraction() const { return lastSpatialBinFraction_; }
  float LastSpectralCenter() const { return lastSpectralCenter_; }
  float LastSpectralTransient() const { return lastSpectralTransient_; }
  const std::array<float, 4>& LastBandOwnership() const { return lastBandOwnership_; }
  const std::array<float, 4>& LastBandCenter() const { return lastBandCenter_; }
  float LastRearBudgetScale() const { return lastRearBudgetScale_; }
  int ProcessingLatencySamples() const { return processingLatencySamples_; }
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

  struct Allpass1
  {
    void Configure(float coefficient);
    void Reset();
    float Process(float x);

    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
  };

  Params params_{};
  OhlSpatialAnalyzer spatialAnalyzer_;
  OhlSpectralRouter spectralRouter_;
  int processingLatencySamples_ = 0;
  std::array<int, kChannels> delaySamples_{{0, 0, 0, 0, 0, 0}};
  std::array<DelayLine, kChannels> delays_;
  DelayLine broadRearDelayL_;
  DelayLine broadRearDelayR_;

  OnePoleLowpass analysisLowL_;
  OnePoleLowpass analysisLowR_;
  OnePoleLowpass analysisMidL_;
  OnePoleLowpass analysisMidR_;

  OnePoleHighpass rearHpL_;
  OnePoleHighpass rearHpR_;
  OnePoleLowpass rearLpL_;
  OnePoleLowpass rearLpR_;
  std::array<Allpass1, 2> rearDecorL_;
  std::array<Allpass1, 2> rearDecorR_;
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
  float lastSpectralAmbience_ = 0.0f;
  float lastSpatialBinFraction_ = 0.0f;
  float lastSpectralCenter_ = 0.0f;
  float lastSpectralTransient_ = 0.0f;
  std::array<float, 4> lastBandOwnership_{{0, 0, 0, 0}};
  std::array<float, 4> lastBandCenter_{{0, 0, 0, 0}};
  float lastRearBudgetScale_ = 1.0f;
  bool spectralPrimed_ = false;
};
