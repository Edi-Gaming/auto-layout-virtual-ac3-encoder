#pragma once

#include <cstddef>
#include <vector>

class OhlSpatialAnalyzer
{
public:
  struct Params
  {
    int sampleRate = 48000;
    int fftSize = 512;
    int hopSize = 256;
    float spatialBinThreshold = 0.30f;
  };

  struct Metrics
  {
    float ambience = 0.0f;
    float center = 0.0f;
    float hardPan = 0.0f;
    float transient = 0.0f;
    float spatialBinFraction = 0.0f;
    float spatialLeftShare = 0.5f;
    float spatialRightShare = 0.5f;
    float meanCoherence = 1.0f;
  };

  bool Init(const Params& params);
  void Reset();

  Metrics Analyze(const float* stereo, size_t frames);

private:
  Params params_{};
  std::vector<float> window_;
  std::vector<float> prevMagL_;
  std::vector<float> prevMagR_;
  bool havePrevious_ = false;
};
