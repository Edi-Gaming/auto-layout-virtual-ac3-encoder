#pragma once
// Original OHL Surround Lab Sparkle Field adapted to the native discrete-5.1 test path.
// Deterministic, allocation-free oscillator voices; LFE is intentionally never written.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

class OhlSparkleField
{
public:
  enum class Texture { Mixed = 0, Sine, Triangle, Metal, Noise };

  void Configure(int sampleRate, int eventsPerSecond, int decayMs, Texture texture)
  {
    sampleRate_ = std::max(8000, sampleRate);
    eventsPerSecond_ = std::clamp(eventsPerSecond, 1, 24);
    decayMs_ = std::clamp(decayMs, 60, 1000);
    texture_ = texture;
    Reset();
  }

  void Reset()
  {
    for (auto& voice : voices_) voice = {};
    nextEvent_ = 0;
    voiceIndex_ = 0;
    noiseState_ = 0x8249AC3Bu;
  }

  // Adds one sample frame in native FFmpeg 5.1 order: FL FR C LFE SL SR.
  void Render(float* outSix, float amplitude)
  {
    if (!outSix) return;
    if (--nextEvent_ <= 0)
    {
      nextEvent_ = std::max(1, sampleRate_ / eventsPerSecond_);
      Voice& v = voices_[voiceIndex_++ % voices_.size()];
      v = {};
      v.total = std::max(1, sampleRate_ * decayMs_ / 1000);
      v.remaining = v.total;
      v.frequency = 700.0 + Random01() * 8300.0;

      // Neighboring-speaker equal-power panning, including the arc across rear.
      constexpr std::array<double, 5> angles{{
          -2.0943951024, -0.3839724354, 0.0, 0.3839724354, 2.0943951024}};
      constexpr std::array<int, 5> channels{{4, 0, 2, 1, 5}};
      const double azimuth = Random01() * kTwoPi - kPi;
      for (size_t i = 0; i < angles.size(); ++i)
      {
        const double next = i == 4 ? angles[0] + kTwoPi : angles[i + 1];
        double a = azimuth;
        if (i == 4 && a < angles[0]) a += kTwoPi;
        if (a >= angles[i] && a < next)
        {
          const double fraction = (a - angles[i]) / (next - angles[i]);
          v.gains[channels[i]] = static_cast<float>(std::sqrt(1.0 - fraction));
          v.gains[channels[(i + 1) % channels.size()]] =
              static_cast<float>(std::sqrt(fraction));
          break;
        }
      }
    }

    // Slightly conservative peak ceiling. Caller also limits requested level.
    for (auto& v : voices_)
    {
      if (v.remaining <= 0) continue;
      const double age = static_cast<double>(v.total - v.remaining);
      const double attack = std::min(1.0, age / (sampleRate_ * 0.003));
      const double envelope = attack * std::exp(-5.0 * age / v.total);
      const double tone = std::sin(v.phase);
      const double noise = 2.0 * Random01() - 1.0;
      double value = 0.0;
      switch (texture_)
      {
        case Texture::Sine: value = tone; break;
        case Texture::Triangle: value = (2.0 / kPi) * std::asin(tone); break;
        case Texture::Metal:
          value = 0.70 * tone + 0.20 * std::sin(v.phase * 1.618) +
                  0.10 * std::sin(v.phase * 2.414); break;
        case Texture::Noise: value = 0.60 * noise; break;
        case Texture::Mixed:
        default:
          value = 0.70 * tone + 0.20 * std::sin(v.phase * 2.03) +
                  0.10 * noise; break;
      }
      const float sample = static_cast<float>(amplitude * 0.45 * envelope * value);
      for (const int ch : {0, 1, 2, 4, 5})
        outSix[ch] += sample * v.gains[ch];

      v.phase += kTwoPi * v.frequency / sampleRate_;
      if (v.phase > kTwoPi) v.phase = std::fmod(v.phase, kTwoPi);
      --v.remaining;
    }
  }

private:
  static constexpr double kPi = 3.14159265358979323846;
  static constexpr double kTwoPi = 2.0 * kPi;
  struct Voice
  {
    int remaining = 0;
    int total = 1;
    double phase = 0.0;
    double frequency = 1000.0;
    std::array<float, 6> gains{};
  };

  double Random01()
  {
    noiseState_ ^= noiseState_ << 13;
    noiseState_ ^= noiseState_ >> 17;
    noiseState_ ^= noiseState_ << 5;
    return static_cast<double>(noiseState_) / 4294967296.0;
  }

  int sampleRate_ = 48000;
  int eventsPerSecond_ = 9;
  int decayMs_ = 240;
  Texture texture_ = Texture::Mixed;
  std::array<Voice, 4> voices_{};
  uint32_t noiseState_ = 0x8249AC3Bu;
  uint32_t voiceIndex_ = 0;
  int nextEvent_ = 0;
};
