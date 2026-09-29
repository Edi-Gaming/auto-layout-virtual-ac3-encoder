// test_ohl_music.cpp — behavioral tests for OHL Music spatializer.
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
  CHECK(d[2] == 11);  // C  30" -> ~10.7 samples
  CHECK(d[3] == 0);   // LFE reference only
  CHECK(d[4] == 21);  // SL 27" -> ~21.3 samples
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

TEST_CASE("OHL Music keeps an audible rear width bed even for highly correlated stereo")
{
  constexpr size_t frames = 1536;
  const auto in = MakeSineStereo(frames, 1200.0, false);

  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  REQUIRE(upmixer.Init(p));

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  const double front = 0.5 * (ChannelRms(out, 0) + ChannelRms(out, 1));
  const double rear = 0.5 * (ChannelRms(out, 4) + ChannelRms(out, 5));

  MESSAGE("correlated stereo front RMS=" << front << " rear width RMS=" << rear);
  CHECK(rear > 0.008);
  CHECK(rear < front * 0.20);
}

TEST_CASE("OHL Music still opens anti-phase ambience much more than its baseline width bed")
{
  constexpr size_t frames = 1536;

  OhlMusicUpmixer ambience;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  REQUIRE(ambience.Init(p));

  const auto anti = MakeSineStereo(frames, 440.0, true);
  std::vector<float> antiOut(frames * 6, 0.0f);
  ambience.ProcessStereo(anti.data(), frames, antiOut.data());

  OhlMusicUpmixer correlated;
  REQUIRE(correlated.Init(p));
  const auto mono = MakeSineStereo(frames, 440.0, false);
  std::vector<float> monoOut(frames * 6, 0.0f);
  correlated.ProcessStereo(mono.data(), frames, monoOut.data());

  const double antiRear = 0.5 * (ChannelRms(antiOut, 4) + ChannelRms(antiOut, 5));
  const double baseRear = 0.5 * (ChannelRms(monoOut, 4) + ChannelRms(monoOut, 5));

  MESSAGE("anti-phase rear RMS=" << antiRear << " baseline rear RMS=" << baseRear);
  CHECK(antiRear > 0.10);
  CHECK(antiRear > 4.0 * baseRear);
}

TEST_CASE("OHL Music does not gate the rear field off on a center transient")
{
  constexpr size_t frames = 1536;
  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  REQUIRE(upmixer.Init(p));

  // First build up a spatial rear field.
  const auto anti = MakeSineStereo(frames, 440.0, true);
  std::vector<float> outA(frames * 6, 0.0f);
  upmixer.ProcessStereo(anti.data(), frames, outA.data());
  const double before = 0.5 * (ChannelRms(outA, 4) + ChannelRms(outA, 5));

  // Then hit it with a loud, highly correlated transient-ish block.
  std::vector<float> transient(frames * 2, 0.0f);
  for (size_t i = 0; i < 24; ++i)
  {
    const float s = 0.85f * static_cast<float>(1.0 - double(i) / 24.0);
    transient[2 * i] = s;
    transient[2 * i + 1] = s;
  }

  std::vector<float> outB(frames * 6, 0.0f);
  upmixer.ProcessStereo(transient.data(), frames, outB.data());
  const double after = 0.5 * (ChannelRms(outB, 4) + ChannelRms(outB, 5));

  MESSAGE("rear RMS before transient=" << before << " during transient=" << after);
  CHECK(before > 0.08);
  CHECK(after > 0.002);
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

  MESSAGE("center RMS 200 Hz=" << lowCenter << " 7 kHz=" << highCenter);
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
  REQUIRE(upmixer.Init(p));
  REQUIRE(upmixer.DelaySamples()[4] == 21);
  REQUIRE(upmixer.DelaySamples()[5] == 0);

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  // SR is at the 33" reference distance and therefore responds immediately.
  CHECK(std::fabs(out[5]) > 0.05f);

  // SL is physically 6" closer, so software adds ~21 samples before the same extracted event.
  for (int i = 0; i < 21; ++i)
    CHECK(std::fabs(out[6 * static_cast<size_t>(i) + 4]) < 1.0e-7f);
  CHECK(std::fabs(out[6 * 21 + 4]) > 0.05f);
}
