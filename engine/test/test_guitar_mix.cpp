#include "doctest.h"
#include "GuitarMix.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

TEST_CASE("Guitar bus mixes float stereo into front channels without changing surrounds")
{
  constexpr GuitarMix::Format format{2, 32, true};
  float guitar[4] = {0.2f, -0.4f, 0.5f, 0.25f};
  std::array<float, 12> music{};
  music[0] = 0.1f;
  music[1] = 0.1f;
  music[2] = 0.02f;
  music[4] = 0.03f;
  music[5] = -0.03f;
  float limiter = 1.0f;
  GuitarMix::AddTo51(music.data(), 2,
                     reinterpret_cast<const uint8_t*>(guitar),
                     format, 0.5f, limiter);
  CHECK(music[0] == doctest::Approx(0.2f));
  CHECK(music[1] == doctest::Approx(-0.1f));
  CHECK(music[6] == doctest::Approx(0.25f));
  CHECK(music[7] == doctest::Approx(0.125f));
  CHECK(music[2] == doctest::Approx(0.02f));
  CHECK(music[4] == doctest::Approx(0.03f));
  CHECK(music[5] == doctest::Approx(-0.03f));
  CHECK(music[3] == 0.0f);
  CHECK(music[9] == 0.0f);
}

TEST_CASE("Mono guitar is reproduced to both fronts; signed PCM16 is decoded")
{
  constexpr GuitarMix::Format format{1, 16, false};
  int16_t pcm[2] = {16384, -16384};
  std::array<float, 12> frame{};
  float limiter = 1.0f;
  GuitarMix::AddTo51(frame.data(), 2,
                     reinterpret_cast<const uint8_t*>(pcm),
                     format, 0.5f, limiter);
  CHECK(frame[0] == doctest::Approx(0.25f));
  CHECK(frame[1] == doctest::Approx(0.25f));
  CHECK(frame[6] == doctest::Approx(-0.25f));
  CHECK(frame[7] == doctest::Approx(-0.25f));
}

TEST_CASE("Guitar mixer limits music plus guitar peak without touching signal topology")
{
  constexpr GuitarMix::Format format{2, 32, true};
  float pcm[2] = {0.9f, 0.9f};
  std::array<float, 6> music{{0.8f, 0.8f, 0.0f, 0.0f, 0.1f, -0.1f}};
  float limiter = 1.0f;
  GuitarMix::AddTo51(music.data(), 1,
                     reinterpret_cast<const uint8_t*>(pcm), format, 1.0f, limiter);
  CHECK(music[0] == doctest::Approx(0.98f));
  CHECK(music[1] == doctest::Approx(0.98f));
  CHECK(music[4] > 0.0f);
  CHECK(music[5] < 0.0f);
  CHECK(limiter < 1.0f);
  CHECK(limiter > 0.0f);
  for (float sample : music) CHECK(std::isfinite(sample));
}

TEST_CASE("Absent guitar bus never injects stale samples, and NaN input is sanitized")
{
  constexpr GuitarMix::Format format{2, 32, true};
  float nan = std::nanf("");
  float pcm[2] = {nan, 0.1f};
  std::array<float, 6> out{};
  float limiter = 1.0f;
  GuitarMix::AddTo51(out.data(), 1,
                     reinterpret_cast<const uint8_t*>(pcm), format, 1.0f, limiter);
  CHECK(out[0] == 0.0f);
  CHECK(out[1] == doctest::Approx(0.1f));
  out.fill(0.0f);
  GuitarMix::AddTo51(out.data(), 1, nullptr, format, 1.0f, limiter);
  for (float sample : out) CHECK(sample == 0.0f);
}
