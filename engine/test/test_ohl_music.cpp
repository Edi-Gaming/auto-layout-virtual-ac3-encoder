// test_ohl_music.cpp — behavioral tests for the conservative OHL Music spatializer.
#include "doctest.h"
#include "OhlMusicUpmixer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

double ChannelRms(const std::vector<float>& x, int ch)
{
  double e = 0.0;
  const size_t frames = x.size() / OhlMusicUpmixer::kChannels;
  for (size_t i = 0; i < frames; ++i)
  {
    const double s = x[OhlMusicUpmixer::kChannels * i + static_cast<size_t>(ch)];
    e += s * s;
  }
  return frames ? std::sqrt(e / static_cast<double>(frames)) : 0.0;
}

OhlMusicUpmixer::Params EqualDistanceParams()
{
  OhlMusicUpmixer::Params p;
  p.distanceInches = {{33.0f, 33.0f, 33.0f, 33.0f, 33.0f, 33.0f}};
  return p;
}

std::vector<float> MakeSineStereo(size_t frames, bool antiPhase, bool rightEnabled = true)
{
  std::vector<float> x(frames * 2);
  for (size_t i = 0; i < frames; ++i)
  {
    const float s =
        0.30f * static_cast<float>(std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / 48000.0));
    x[2 * i] = s;
    x[2 * i + 1] = rightEnabled ? (antiPhase ? -s : s) : 0.0f;
  }
  return x;
}

} // namespace

TEST_CASE("OHL Music default speaker distances produce Edi's measured alignment delays")
{
  OhlMusicUpmixer upmixer;
  OhlMusicUpmixer::Params p;
  REQUIRE(upmixer.Init(p));

  const auto& d = upmixer.DelaySamples();
  CHECK(d[0] == 0);   // FL 33"
  CHECK(d[1] == 0);   // FR 33"
  CHECK(d[2] == 11);  // C  30" -> ~10.7 samples
  CHECK(d[3] == 0);   // LFE reference only; silent in v0.1
  CHECK(d[4] == 21);  // SL 27" -> ~21.3 samples
  CHECK(d[5] == 0);   // SR 33"
}

TEST_CASE("OHL Music keeps correlated center information in the front phantom image")
{
  constexpr size_t frames = 1536;
  const auto in = MakeSineStereo(frames, false);

  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  REQUIRE(upmixer.Init(p));

  std::vector<float> out(frames * OhlMusicUpmixer::kChannels, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  double maxFrontError = 0.0;
  double maxCenter = 0.0;
  double maxLfe = 0.0;
  double maxSurround = 0.0;
  for (size_t i = 0; i < frames; ++i)
  {
    maxFrontError = std::max(
        maxFrontError,
        std::fabs(static_cast<double>(out[6 * i] - in[2 * i])));
    maxFrontError = std::max(
        maxFrontError,
        std::fabs(static_cast<double>(out[6 * i + 1] - in[2 * i + 1])));
    maxCenter = std::max(maxCenter, std::fabs(static_cast<double>(out[6 * i + 2])));
    maxLfe = std::max(maxLfe, std::fabs(static_cast<double>(out[6 * i + 3])));
    maxSurround = std::max(maxSurround, std::fabs(static_cast<double>(out[6 * i + 4])));
    maxSurround = std::max(maxSurround, std::fabs(static_cast<double>(out[6 * i + 5])));
  }

  CHECK(maxFrontError < 1.0e-7);
  CHECK(maxCenter < 1.0e-7);
  CHECK(maxLfe < 1.0e-7);
  CHECK(maxSurround < 1.0e-6);
}

TEST_CASE("OHL Music opens balanced anti-phase ambience much more than a hard-panned source")
{
  constexpr size_t frames = 1536;

  OhlMusicUpmixer ambience;
  auto p = EqualDistanceParams();
  REQUIRE(ambience.Init(p));
  const auto anti = MakeSineStereo(frames, true);
  std::vector<float> antiOut(frames * 6, 0.0f);
  ambience.ProcessStereo(anti.data(), frames, antiOut.data());

  OhlMusicUpmixer hardLeft;
  REQUIRE(hardLeft.Init(p));
  const auto left = MakeSineStereo(frames, false, false);
  std::vector<float> leftOut(frames * 6, 0.0f);
  hardLeft.ProcessStereo(left.data(), frames, leftOut.data());

  const double antiRear = 0.5 * (ChannelRms(antiOut, 4) + ChannelRms(antiOut, 5));
  const double leftRear = 0.5 * (ChannelRms(leftOut, 4) + ChannelRms(leftOut, 5));

  MESSAGE("anti-phase rear RMS=" << antiRear << " hard-left rear RMS=" << leftRear);
  CHECK(antiRear > 0.08);
  CHECK(leftRear < 0.01);
  CHECK(antiRear > 10.0 * leftRear);
}

TEST_CASE("OHL Music applies speaker-distance delay after spatial extraction")
{
  constexpr size_t frames = 128;
  std::vector<float> in(frames * 2, 0.0f);
  in[0] = 1.0f;
  in[1] = -1.0f;

  OhlMusicUpmixer upmixer;
  OhlMusicUpmixer::Params p;
  REQUIRE(upmixer.Init(p));
  REQUIRE(upmixer.DelaySamples()[4] == 21);
  REQUIRE(upmixer.DelaySamples()[5] == 0);

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  // SR is at the 33" reference distance and therefore responds immediately.
  CHECK(std::fabs(out[5]) > 0.1f);

  // SL is physically 6" closer, so software adds ~21 samples before the same extracted event.
  for (int i = 0; i < 21; ++i)
    CHECK(std::fabs(out[6 * static_cast<size_t>(i) + 4]) < 1.0e-7f);
  CHECK(std::fabs(out[6 * 21 + 4]) > 0.1f);

  // Physical center and LFE remain intentionally unused in v0.1.
  for (size_t i = 0; i < frames; ++i)
  {
    CHECK(out[6 * i + 2] == doctest::Approx(0.0f));
    CHECK(out[6 * i + 3] == doctest::Approx(0.0f));
  }
}
