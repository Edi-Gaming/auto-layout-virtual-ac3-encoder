#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

class OhlSpectralRouter
{
public:
  static constexpr int kBands = 4;

  struct Params
  {
    int sampleRate = 48000;
    int fftSize = 512;
    int hopSize = 256;

    float spatialThreshold = 0.30f;
    float acquireMs = 65.0f;
    float releaseMs = 520.0f;

    float surroundGain = 0.70f;
    float widthFloor = 0.16f;
    float dimension = 0.0f; // -1 front-biased, +1 rear-biased

    // Low (<250), body (250-2k), presence (2-6k), air (>6k).
    std::array<float, kBands> steering{{0.18f, 0.55f, 0.90f, 1.10f}};
    std::array<float, kBands> frontLock{{0.30f, 1.00f, 0.82f, 0.25f}};
    float globalFrontLock = 0.88f;

    float directReject = 0.78f;
  };

  struct Metrics
  {
    std::array<float, kBands> ownership{{0, 0, 0, 0}};
    std::array<float, kBands> center{{0, 0, 0, 0}};
    float meanOwnership = 0.0f;
    float activeFraction = 0.0f;
    float transient = 0.0f;
  };

  bool Init(const Params& params);
  void Reset();

  // Streaming stereo -> rear pair. Output contains a fixed hopSize-sample latency.
  void Process(const float* stereo, size_t frames, float* rearL, float* rearR);

  int LatencySamples() const { return params_.hopSize; }
  const Metrics& LastMetrics() const { return metrics_; }

private:
  static int BandForHz(double hz);
  static void Fft(std::vector<std::complex<double>>& a, bool inverse);
  std::complex<double> AllpassResponse(double omega, double a) const;
  void ProcessFrame();

  Params params_{};
  Metrics metrics_{};

  std::vector<float> window_;
  std::vector<float> inL_;
  std::vector<float> inR_;
  std::vector<float> olaL_;
  std::vector<float> olaR_;
  std::vector<float> outQueueL_;
  std::vector<float> outQueueR_;
  size_t outRead_ = 0;
  int initialSilence_ = 0;

  std::vector<double> smoothPL_;
  std::vector<double> smoothPR_;
  std::vector<std::complex<double>> smoothCross_;
  std::vector<float> prevMagL_;
  std::vector<float> prevMagR_;
  std::vector<float> ownership_;
};
