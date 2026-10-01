#pragma once

#include <array>
#include <atomic>
#include <cstdio>
#include <string>

struct MusicTelemetry
{
  std::atomic<float> ambience{0.0f};
  std::atomic<float> center{0.0f};
  std::atomic<float> spatialBins{0.0f};
  std::atomic<float> transient{0.0f};
  std::atomic<float> surroundAmount{0.0f};
  std::atomic<float> frontLock{0.0f};
  std::atomic<float> rearBudgetScale{1.0f};

  std::array<std::atomic<float>, 4> bandOwnership{
      std::atomic<float>{0.0f}, std::atomic<float>{0.0f},
      std::atomic<float>{0.0f}, std::atomic<float>{0.0f}};
  std::array<std::atomic<float>, 4> bandCenter{
      std::atomic<float>{0.0f}, std::atomic<float>{0.0f},
      std::atomic<float>{0.0f}, std::atomic<float>{0.0f}};

  void Reset()
  {
    ambience.store(0.0f);
    center.store(0.0f);
    spatialBins.store(0.0f);
    transient.store(0.0f);
    surroundAmount.store(0.0f);
    frontLock.store(0.0f);
    rearBudgetScale.store(1.0f);
    for (auto& v : bandOwnership) v.store(0.0f);
    for (auto& v : bandCenter) v.store(0.0f);
  }

  std::string ToCompactString() const
  {
    char buf[768] = {};
    std::snprintf(
        buf, sizeof(buf),
        "amb=%.3f;center=%.3f;bins=%.3f;trans=%.3f;rear=%.3f;lock=%.3f;budget=%.3f;"
        "own=%.3f,%.3f,%.3f,%.3f;bandcenter=%.3f,%.3f,%.3f,%.3f",
        ambience.load(), center.load(), spatialBins.load(), transient.load(),
        surroundAmount.load(), frontLock.load(), rearBudgetScale.load(),
        bandOwnership[0].load(), bandOwnership[1].load(),
        bandOwnership[2].load(), bandOwnership[3].load(),
        bandCenter[0].load(), bandCenter[1].load(),
        bandCenter[2].load(), bandCenter[3].load());
    return std::string(buf);
  }
};
