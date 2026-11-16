// GuitarMix.h - allocation-free second-source PCM mixer for OHL's AC-3 render thread.
// Both sources are sampled at the encoder rate. The primary music bus has already
// been spatialized; guitar is intentionally inserted afterward into FL/FR only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace GuitarMix {

struct Format
{
  unsigned channels = 0;
  unsigned bits = 0;
  bool isFloat = false;
};

inline bool Supported(const Format& f)
{
  return (f.channels == 1 || f.channels == 2) &&
         ((f.isFloat && f.bits == 32) ||
          (!f.isFloat && (f.bits == 16 || f.bits == 32)));
}

inline float Sample(const uint8_t* pcm, size_t frame, unsigned channel,
                    const Format& fmt)
{
  if (!pcm || fmt.channels == 0 || fmt.bits == 0 || channel >= fmt.channels)
    return 0.0f;
  const size_t offset =
      (frame * static_cast<size_t>(fmt.channels) + channel) * (fmt.bits / 8u);
  const uint8_t* ptr = pcm + offset;
  if (fmt.isFloat && fmt.bits == 32)
  {
    float value = 0.0f;
    std::memcpy(&value, ptr, sizeof value);
    return std::isfinite(value) ? value : 0.0f;
  }
  if (!fmt.isFloat && fmt.bits == 16)
  {
    int16_t value = 0;
    std::memcpy(&value, ptr, sizeof value);
    return static_cast<float>(value / 32768.0);
  }
  if (!fmt.isFloat && fmt.bits == 32)
  {
    int32_t value = 0;
    std::memcpy(&value, ptr, sizeof value);
    return static_cast<float>(static_cast<double>(value) / 2147483648.0);
  }
  return 0.0f;
}

// output51 is Windows/FFmpeg 5.1_BACK order: FL FR C LFE BL BR.
// A packetwise peak limiter ensures simultaneous loud sources cannot overflow
// the AC-3 encoder. Recovery is gradual, but attack is immediate.
inline void AddTo51(float* output51, size_t frames, const uint8_t* guitarPcm,
                    const Format& guitarFormat, float guitarGain,
                    float& limiterGain)
{
  if (!output51) return;
  if (guitarPcm && Supported(guitarFormat))
  {
    for (size_t frame = 0; frame < frames; ++frame)
    {
      const float left = Sample(guitarPcm, frame, 0, guitarFormat);
      const float right = guitarFormat.channels > 1
          ? Sample(guitarPcm, frame, 1, guitarFormat) : left;
      output51[6 * frame] += left * guitarGain;
      output51[6 * frame + 1] += right * guitarGain;
    }
  }

  float peak = 0.0f;
  for (size_t i = 0; i < frames * 6; ++i)
  {
    float& x = output51[i];
    if (!std::isfinite(x)) x = 0.0f;
    peak = (std::max)(peak, std::fabs(x));
  }
  const float requiredGain = peak > 0.98f ? 0.98f / peak : 1.0f;
  limiterGain = (std::min)(requiredGain, limiterGain + 0.04f * (1.0f - limiterGain));
  limiterGain = (std::clamp)(limiterGain, 0.0f, 1.0f);
  for (size_t i = 0; i < frames * 6; ++i)
    output51[i] *= limiterGain;
}

} // namespace GuitarMix
