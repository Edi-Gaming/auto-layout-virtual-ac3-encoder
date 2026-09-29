// test_ohl_music.cpp — behavioral tests for OHL Music spatializer.
#include "doctest.h"
#include "OhlMusicUpmixer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

double ChannelRms(const std::vector<float>& x, int ch, size_t startFrame = 0)
{
  double e = 0.0;
  const size_t frames = x.size() / OhlMusicUpmixer::kChannels;
  if (startFrame >= frames) return 0.0;
  for (size_t i = startFrame; i < frames; ++i)
  {
    const double s = x[OhlMusicUpmixer::kChannels * i + static_cast<size_t>(ch)];
    e += s * s;
  }
  return std::sqrt(e / static_cast<double>(frames - startFrame));
}

double ChannelPeak(const std::vector<float>& x, int ch)
{
  double peak = 0.0;
  const size_t frames = x.size() / OhlMusicUpmixer::kChannels;
  for (size_t i = 0; i < frames; ++i)
    peak = std::max(peak, std::fabs(static_cast<double>(x[6 * i + static_cast<size_t>(ch)])));
  return peak;
}

OhlMusicUpmixer::Params EqualDistanceParams()
{
  OhlMusicUpmixer::Params p;
  p.distanceInches = {{33.0f, 33.0f, 33.0f, 33.0f, 33.0f, 33.0f}};
  return p;
}

std::vector<float> MakeSineStereo(size_t frames,
                                  double hz,
                                  bool antiPhase,
                                  bool rightEnabled = true,
                                  float amplitude = 0.30f)
{
  std::vector<float> x(frames * 2);
  for (size_t i = 0; i < frames; ++i)
  {
    const float s =
        amplitude * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / 48000.0));
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
  CHECK(d[2] == 11);  // C  30"
  CHECK(d[3] == 0);
  CHECK(d[4] == 21);  // SL 27"
  CHECK(d[5] == 0);   // SR 33"
}

TEST_CASE("OHL Music preserves FL/FR exactly while keeping LFE silent")
{
  constexpr size_t frames = 1536;
  const auto in = MakeSineStereo(frames, 440.0, false);

  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  REQUIRE(upmixer.Init(p));

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  double maxFrontError = 0.0;
  double maxLfe = 0.0;
  for (size_t i = 0; i < frames; ++i)
  {
    maxFrontError = std::max(
        maxFrontError, std::fabs(static_cast<double>(out[6 * i] - in[2 * i])));
    maxFrontError = std::max(
        maxFrontError, std::fabs(static_cast<double>(out[6 * i + 1] - in[2 * i + 1])));
    maxLfe = std::max(maxLfe, std::fabs(static_cast<double>(out[6 * i + 3])));
  }

  CHECK(maxFrontError < 1.0e-7);
  CHECK(maxLfe < 1.0e-7);
}

TEST_CASE("OHL Music does not synthesize rear energy from centered mono program")
{
  constexpr size_t frames = 1536;
  const auto in = MakeSineStereo(frames, 1200.0, false);

  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.widthFloor = 0.30f;
  p.surroundGain = 1.0f;
  REQUIRE(upmixer.Init(p));

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  CHECK(ChannelRms(out, 4) < 1.0e-6);
  CHECK(ChannelRms(out, 5) < 1.0e-6);
}

TEST_CASE("OHL Music base width passes subtle stereo difference without copying the center")
{
  constexpr size_t frames = 1536;
  std::vector<float> in(frames * 2);
  for (size_t i = 0; i < frames; ++i)
  {
    const double t = static_cast<double>(i) / 48000.0;
    const float common = 0.25f * static_cast<float>(std::sin(2.0 * kPi * 700.0 * t));
    const float stereoDetail = 0.035f * static_cast<float>(std::sin(2.0 * kPi * 3100.0 * t));
    in[2 * i] = common + stereoDetail;
    in[2 * i + 1] = common - stereoDetail;
  }

  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.widthFloor = 0.16f;
  p.surroundGain = 0.0f;
  p.directReject = 0.0f;
  REQUIRE(upmixer.Init(p));

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  const double rear = 0.5 * (ChannelRms(out, 4) + ChannelRms(out, 5));
  CHECK(rear > 0.002);
  CHECK(rear < 0.015);
}

TEST_CASE("OHL Music opens diffuse anti-phase ambience strongly")
{
  constexpr size_t frames = 1536;

  OhlMusicUpmixer ambience;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.directReject = 0.0f;
  REQUIRE(ambience.Init(p));

  const auto anti = MakeSineStereo(frames, 1800.0, true);
  std::vector<float> out(frames * 6, 0.0f);
  ambience.ProcessStereo(anti.data(), frames, out.data());

  const double rear = 0.5 * (ChannelRms(out, 4, 256) + ChannelRms(out, 5, 256));
  CHECK(rear > 0.08);
}

TEST_CASE("OHL Music direct-event rejection keeps a hard-panned clap onset out of the rears")
{
  constexpr size_t frames = 512;
  std::vector<float> clap(frames * 2, 0.0f);
  for (size_t i = 0; i < 32; ++i)
    clap[2 * i] = 0.95f * static_cast<float>(1.0 - static_cast<double>(i) / 32.0);

  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.widthFloor = 0.25f;
  p.surroundGain = 0.0f;

  OhlMusicUpmixer unprotected;
  p.directReject = 0.0f;
  REQUIRE(unprotected.Init(p));
  std::vector<float> openOut(frames * 6, 0.0f);
  unprotected.ProcessStereo(clap.data(), frames, openOut.data());

  OhlMusicUpmixer protectedMixer;
  p.directReject = 0.90f;
  REQUIRE(protectedMixer.Init(p));
  std::vector<float> protectedOut(frames * 6, 0.0f);
  protectedMixer.ProcessStereo(clap.data(), frames, protectedOut.data());

  const double openPeak = ChannelPeak(openOut, 4);
  const double protectedPeak = ChannelPeak(protectedOut, 4);
  MESSAGE("rear clap peak open=" << openPeak << " protected=" << protectedPeak);
  CHECK(openPeak > 0.05);
  CHECK(protectedPeak < openPeak * 0.45);
}

TEST_CASE("OHL Music sustained ambience survives after the onset detector settles")
{
  constexpr size_t frames = 4096;
  const auto anti = MakeSineStereo(frames, 2200.0, true);

  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.directReject = 0.90f;
  REQUIRE(upmixer.Init(p));

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(anti.data(), frames, out.data());

  const double settledRear = 0.5 * (ChannelRms(out, 4, 2048) + ChannelRms(out, 5, 2048));
  CHECK(settledRear > 0.06);
}

TEST_CASE("OHL Music center sparkle strongly favors treble over low-frequency center content")
{
  constexpr size_t frames = 1536;

  OhlMusicUpmixer low;
  auto p = EqualDistanceParams();
  p.widthFloor = 0.0f;
  p.surroundGain = 0.0f;
  p.centerTrebleGain = 0.25f;
  REQUIRE(low.Init(p));

  const auto lowIn = MakeSineStereo(frames, 200.0, false);
  std::vector<float> lowOut(frames * 6, 0.0f);
  low.ProcessStereo(lowIn.data(), frames, lowOut.data());

  OhlMusicUpmixer high;
  REQUIRE(high.Init(p));
  const auto highIn = MakeSineStereo(frames, 7000.0, false);
  std::vector<float> highOut(frames * 6, 0.0f);
  high.ProcessStereo(highIn.data(), frames, highOut.data());

  const double lowCenter = ChannelRms(lowOut, 2);
  const double highCenter = ChannelRms(highOut, 2);

  CHECK(highCenter > 0.03);
  CHECK(highCenter > 4.0 * lowCenter);
}

TEST_CASE("OHL Music applies speaker-distance delay after spatial extraction")
{
  constexpr size_t frames = 128;
  std::vector<float> in(frames * 2, 0.0f);
  in[0] = 1.0f;
  in[1] = -1.0f;

  OhlMusicUpmixer upmixer;
  OhlMusicUpmixer::Params p;
  p.centerTrebleGain = 0.0f;
  p.directReject = 0.0f;
  REQUIRE(upmixer.Init(p));
  REQUIRE(upmixer.DelaySamples()[4] == 21);
  REQUIRE(upmixer.DelaySamples()[5] == 0);

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  CHECK(std::fabs(out[5]) > 0.02f);
  for (int i = 0; i < 21; ++i)
    CHECK(std::fabs(out[6 * static_cast<size_t>(i) + 4]) < 1.0e-7f);
  CHECK(std::fabs(out[6 * 21 + 4]) > 0.02f);
}
