#pragma once
// Native, allocation-free port of OHL Surround Lab v0.2's Sparkle Field.
// The generator runs only in the pre-existing 5.1 diagnostic override.
// Channel order: FL FR C LFE SL SR. No LFE events are synthesized.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

class OhlSparkleField
{
public:
  enum class Texture { Mixed = 0, Sine, Triangle, Metal, Noise };

  void Configure(int sampleRate, int eventsPerSecond, int decayMs, Texture texture,
                 int lowHz = 700, int highHz = 9000)
  {
    sampleRate_ = (std::max)(8000, sampleRate);
    eventsPerSecond_ = std::clamp(eventsPerSecond, 1, 30);
    decayMs_ = std::clamp(decayMs, 40, 1200);
    lowHz_ = std::clamp(lowHz, 150, 8000);
    highHz_ = std::clamp(highHz, (std::max)(lowHz_ + 200, 500), 18000);
    texture_ = texture;
    Reset();
  }

  void Reset()
  {
    for (auto& v : voices_) v = {};
    nextEvent_ = 0;
    voiceIndex_ = 0;
    noiseState_ = 0x8249AC3Bu;
  }

  // Adds a single rendered frame in native FFmpeg channel order; output
  // contains no additional allocations, locks or driver operations.
  void Render(float* outSix, float amplitude)
  {
    if (!outSix) return;
    if (--nextEvent_ <= 0)
    {
      nextEvent_ = (std::max)(1, sampleRate_ / eventsPerSecond_);
      Voice& v = voices_[voiceIndex_++ % voices_.size()];
      v = {};
      v.total = (std::max)(1, static_cast<int>(
          sampleRate_ * decayMs_ * (0.72 + 0.56 * Random01()) / 1000.0));
      v.remaining = v.total;
      const double low = static_cast<double>(lowHz_);
      v.frequency = low * std::pow(highHz_ / low, Random01()); // v0.2 logarithmic selection
      v.glideRatio = 0.90 + 0.22 * Random01();
      v.peak = 0.55 + 0.45 * Random01();

      v.texture = texture_;
      if (texture_ == Texture::Mixed)
      {
        const double which = Random01();
        v.texture = which < 0.34 ? Texture::Sine :
                    which < 0.62 ? Texture::Triangle :
                    which < 0.82 ? Texture::Metal : Texture::Noise;
      }

      // Original v0.2 four-corner equal-power ring with smoothly blended
      // center channel over the forward 65-degree window.
      constexpr std::array<double, 4> az{{-110.0, -30.0, 30.0, 110.0}};
      constexpr std::array<int, 4> ch{{4, 0, 1, 5}};
      const double angleDeg = -180.0 + 360.0 * Random01();
      double circle = angleDeg;
      if (circle < az[0]) circle += 360.0;
      for (size_t i = 0; i < ch.size(); ++i)
      {
        const double left = az[i];
        const double right = i + 1 < ch.size() ? az[i + 1] : az[0] + 360.0;
        if (circle >= left && circle <= right)
        {
          const double t = std::clamp((circle - left) / (right - left), 0.0, 1.0);
          v.gains[ch[i]] = static_cast<float>(std::cos(t * kPi * 0.5));
          v.gains[ch[(i + 1) % ch.size()]] =
              static_cast<float>(std::sin(t * kPi * 0.5));
          break;
        }
      }
      const double center = std::abs(angleDeg) >= 65.0 ? 0.0 :
          std::cos(std::abs(angleDeg) * kPi / (65.0 * 2.0));
      const float ringScale = static_cast<float>(
          std::sqrt((std::max)(0.0, 1.0 - center * center)));
      for (const int channel : {0, 1, 4, 5}) v.gains[channel] *= ringScale;
      v.gains[2] = static_cast<float>(center);

      if (v.texture == Texture::Noise)
      {
        const double f = (std::min)(v.frequency, sampleRate_ * 0.47);
        const double q = 7.0 + 8.0 * Random01();
        const double omega = kTwoPi * f / sampleRate_;
        const double alpha = std::sin(omega) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        v.bpB0 = alpha / a0;
        v.bpA1 = -2.0 * std::cos(omega) / a0;
        v.bpA2 = (1.0 - alpha) / a0;
      }
    }

    for (auto& v : voices_)
    {
      if (v.remaining <= 0) continue;
      const double age = static_cast<double>(v.total - v.remaining);
      const double attackSamples = (std::max)(1.0, sampleRate_ * 0.006);
      const double decaySamples = (std::max)(1.0, v.total - attackSamples);
      const double envelope = age < attackSamples
          ? 1e-4 * std::pow(1e4, age / attackSamples)
          : std::pow(1e-4, (age - attackSamples) / decaySamples);

      const double glideSamples = (std::max)(
          1.0, (std::min)(0.2, v.total * 0.8 / sampleRate_) * sampleRate_);
      const double ratio = std::pow(v.glideRatio, (std::min)(1.0, age / glideSamples));
      const double tone = std::sin(v.phase);
      double value = 0.0;
      switch (v.texture)
      {
        case Texture::Triangle:
          value = (2.0 / kPi) * std::asin(tone);
          break;
        case Texture::Metal:
          value = tone;
          if (age < 0.78 * v.total)
            value += 0.26 * std::sin(v.overtonePhase);
          break;
        case Texture::Noise:
        {
          // RBJ constant-peak bandpass, Q=7..15, matching the original v0.2
          // WebAudio bandpass flecks instead of unfiltered white noise.
          const double input = 2.0 * Random01() - 1.0;
          value = v.bpB0 * (input - v.bpX2) -
                  v.bpA1 * v.bpY1 - v.bpA2 * v.bpY2;
          v.bpX2 = v.bpX1;
          v.bpX1 = input;
          v.bpY2 = v.bpY1;
          v.bpY1 = value;
          value *= 2.5;
          break;
        }
        case Texture::Sine:
        case Texture::Mixed:
        default:
          value = tone;
          break;
      }
      const float sample = static_cast<float>(
          amplitude * 0.45 * v.peak * envelope * value);
      for (const int ch : {0, 1, 2, 4, 5})
        outSix[ch] += sample * v.gains[ch];
      v.phase += kTwoPi * v.frequency * ratio / sampleRate_;
      v.overtonePhase += kTwoPi * v.frequency * 2.414 / sampleRate_;
      if (v.phase > kTwoPi) v.phase = std::fmod(v.phase, kTwoPi);
      if (v.overtonePhase > kTwoPi)
        v.overtonePhase = std::fmod(v.overtonePhase, kTwoPi);
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
    double overtonePhase = 0.0;
    double frequency = 1000.0;
    double glideRatio = 1.0;
    double peak = 1.0;
    Texture texture = Texture::Sine;
    double bpB0 = 0.0;
    double bpA1 = 0.0;
    double bpA2 = 0.0;
    double bpX1 = 0.0;
    double bpX2 = 0.0;
    double bpY1 = 0.0;
    double bpY2 = 0.0;
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
  int lowHz_ = 700;
  int highHz_ = 9000;
  Texture texture_ = Texture::Mixed;
  std::array<Voice, 4> voices_{};
  uint32_t noiseState_ = 0x8249AC3Bu;
  uint32_t voiceIndex_ = 0;
  int nextEvent_ = 0;
};
