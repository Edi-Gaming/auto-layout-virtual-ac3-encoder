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
  p.frontLock = 0.0f;
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


TEST_CASE("OHL Music ambience band weights control high-frequency spatial extraction")
{
  constexpr size_t frames = 1536;
  const auto highIn = MakeSineStereo(frames, 7000.0, true);

  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.widthFloor = 0.0f;
  p.directReject = 0.0f;
  p.ambienceLowWeight = 0.0f;
  p.ambienceMidWeight = 0.0f;
  p.ambienceHighWeight = 1.0f;

  OhlMusicUpmixer enabled;
  REQUIRE(enabled.Init(p));
  std::vector<float> enabledOut(frames * 6, 0.0f);
  enabled.ProcessStereo(highIn.data(), frames, enabledOut.data());

  p.ambienceHighWeight = 0.0f;
  OhlMusicUpmixer disabled;
  REQUIRE(disabled.Init(p));
  std::vector<float> disabledOut(frames * 6, 0.0f);
  disabled.ProcessStereo(highIn.data(), frames, disabledOut.data());

  const double enabledRear =
      0.5 * (ChannelRms(enabledOut, 4) + ChannelRms(enabledOut, 5));
  const double disabledRear =
      0.5 * (ChannelRms(disabledOut, 4) + ChannelRms(disabledOut, 5));

  MESSAGE("high-band enabled rear RMS=" << enabledRear << " disabled=" << disabledRear);
  CHECK(enabledRear > 0.05);
  CHECK(disabledRear < 1.0e-6);
}

TEST_CASE("OHL Music ambience attack time controls how quickly the rear opens")
{
  constexpr size_t frames = 1536;
  const auto mono = MakeSineStereo(frames, 1000.0, false);
  const auto diffuse = MakeSineStereo(frames, 2500.0, true);

  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.widthFloor = 0.0f;
  p.directReject = 0.0f;

  OhlMusicUpmixer fast;
  p.ambienceAttackMs = 20.0f;
  REQUIRE(fast.Init(p));
  std::vector<float> scratch(frames * 6, 0.0f);
  fast.ProcessStereo(mono.data(), frames, scratch.data());
  std::vector<float> fastOut(frames * 6, 0.0f);
  fast.ProcessStereo(diffuse.data(), frames, fastOut.data());

  OhlMusicUpmixer slow;
  p.ambienceAttackMs = 1200.0f;
  REQUIRE(slow.Init(p));
  std::fill(scratch.begin(), scratch.end(), 0.0f);
  slow.ProcessStereo(mono.data(), frames, scratch.data());
  std::vector<float> slowOut(frames * 6, 0.0f);
  slow.ProcessStereo(diffuse.data(), frames, slowOut.data());

  const double fastRear = 0.5 * (ChannelRms(fastOut, 4) + ChannelRms(fastOut, 5));
  const double slowRear = 0.5 * (ChannelRms(slowOut, 4) + ChannelRms(slowOut, 5));

  MESSAGE("attack fast rear RMS=" << fastRear << " slow=" << slowRear);
  CHECK(fastRear > slowRear * 2.0);
}

TEST_CASE("OHL Music rear trims independently balance SL and SR")
{
  constexpr size_t frames = 1536;
  const auto in = MakeSineStereo(frames, 2400.0, true);

  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.directReject = 0.0f;
  p.rearLeftTrim = 0.50f;
  p.rearRightTrim = 1.50f;
  REQUIRE(upmixer.Init(p));

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  const double left = ChannelRms(out, 4, 128);
  const double right = ChannelRms(out, 5, 128);
  MESSAGE("rear trims left=" << left << " right=" << right);
  CHECK(right > left * 2.5);
  CHECK(right < left * 3.5);
}

TEST_CASE("OHL Music rear low-pass can darken high-frequency surround detail")
{
  constexpr size_t frames = 4096;
  const auto highIn = MakeSineStereo(frames, 9000.0, true);

  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.directReject = 0.0f;

  OhlMusicUpmixer open;
  p.rearLowpassHz = 20000.0f;
  REQUIRE(open.Init(p));
  std::vector<float> openOut(frames * 6, 0.0f);
  open.ProcessStereo(highIn.data(), frames, openOut.data());

  OhlMusicUpmixer dark;
  p.rearLowpassHz = 2500.0f;
  REQUIRE(dark.Init(p));
  std::vector<float> darkOut(frames * 6, 0.0f);
  dark.ProcessStereo(highIn.data(), frames, darkOut.data());

  const double openRear = 0.5 * (ChannelRms(openOut, 4, 512) + ChannelRms(openOut, 5, 512));
  const double darkRear = 0.5 * (ChannelRms(darkOut, 4, 512) + ChannelRms(darkOut, 5, 512));

  MESSAGE("rear LP open=" << openRear << " dark=" << darkRear);
  CHECK(openRear > darkRear * 2.0);
}

TEST_CASE("OHL Music center low-pass bounds the sparkle band")
{
  constexpr size_t frames = 4096;

  auto p = EqualDistanceParams();
  p.widthFloor = 0.0f;
  p.surroundGain = 0.0f;
  p.centerTrebleGain = 0.40f;
  p.centerTrebleHz = 2000.0f;
  p.centerLowpassHz = 5000.0f;

  OhlMusicUpmixer presence;
  REQUIRE(presence.Init(p));
  const auto presenceIn = MakeSineStereo(frames, 3500.0, false);
  std::vector<float> presenceOut(frames * 6, 0.0f);
  presence.ProcessStereo(presenceIn.data(), frames, presenceOut.data());

  OhlMusicUpmixer extreme;
  REQUIRE(extreme.Init(p));
  const auto extremeIn = MakeSineStereo(frames, 14000.0, false);
  std::vector<float> extremeOut(frames * 6, 0.0f);
  extreme.ProcessStereo(extremeIn.data(), frames, extremeOut.data());

  const double presenceCenter = ChannelRms(presenceOut, 2, 512);
  const double extremeCenter = ChannelRms(extremeOut, 2, 512);

  MESSAGE("center band presence=" << presenceCenter << " extreme=" << extremeCenter);
  CHECK(presenceCenter > extremeCenter * 1.5);
}



TEST_CASE("OHL Music v0.6 exact shared center is removed before rear extraction")
{
  constexpr size_t frames = 4096;
  std::vector<float> in(frames * 2);
  for (size_t i = 0; i < frames; ++i)
  {
    const double tt = static_cast<double>(i) / 48000.0;
    const float sharedVoice =
        0.30f * static_cast<float>(std::sin(2.0 * kPi * 1100.0 * tt));
    // Same centered voice, but the left side carries extra production texture.
    const float leftTexture =
        0.035f * static_cast<float>(std::sin(2.0 * kPi * 7800.0 * tt));
    in[2 * i] = sharedVoice + leftTexture;
    in[2 * i + 1] = sharedVoice;
  }

  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.surroundGain = 0.0f;
  p.widthFloor = 0.30f;
  p.frontLock = 1.0f;
  p.directReject = 0.0f;
  REQUIRE(upmixer.Init(p));

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  const double left = ChannelRms(out, 4, 512);
  const double right = ChannelRms(out, 5, 512);
  MESSAGE("shared-center carve rear RMS left=" << left << " right=" << right);

  CHECK(left > 0.003);
  CHECK(right < 1.0e-5);
}

TEST_CASE("OHL Music v0.6 front lock directly carves residual vocal-band energy")
{
  constexpr size_t frames = 4096;
  std::vector<float> in(frames * 2);
  for (size_t i = 0; i < frames; ++i)
  {
    const double tt = static_cast<double>(i) / 48000.0;
    const float vocal =
        0.30f * static_cast<float>(std::sin(2.0 * kPi * 1000.0 * tt));
    const float stereoResidue =
        0.045f * static_cast<float>(std::sin(2.0 * kPi * 1450.0 * tt));
    in[2 * i] = vocal + stereoResidue;
    in[2 * i + 1] = vocal - stereoResidue;
  }

  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.surroundGain = 0.0f;
  p.widthFloor = 0.28f;
  p.directReject = 0.0f;
  p.rearHighpassHz = 80.0f;
  p.rearLowpassHz = 18000.0f;

  OhlMusicUpmixer unlocked;
  p.frontLock = 0.0f;
  REQUIRE(unlocked.Init(p));
  std::vector<float> unlockedOut(frames * 6, 0.0f);
  unlocked.ProcessStereo(in.data(), frames, unlockedOut.data());

  OhlMusicUpmixer locked;
  p.frontLock = 0.95f;
  REQUIRE(locked.Init(p));
  std::vector<float> lockedOut(frames * 6, 0.0f);
  locked.ProcessStereo(in.data(), frames, lockedOut.data());

  const double unlockedRear =
      0.5 * (ChannelRms(unlockedOut, 4, 512) + ChannelRms(unlockedOut, 5, 512));
  const double lockedRear =
      0.5 * (ChannelRms(lockedOut, 4, 512) + ChannelRms(lockedOut, 5, 512));

  MESSAGE("front-lock vocal-like rear RMS unlocked=" << unlockedRear << " locked=" << lockedRear);
  CHECK(unlockedRear > 0.003);
  CHECK(lockedRear < unlockedRear * 0.15);
}

TEST_CASE("OHL Music v0.6 front lock leaves high-frequency diffuse ambience untouched")
{
  constexpr size_t frames = 4096;
  const auto in = MakeSineStereo(frames, 9000.0, true);

  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.widthFloor = 0.12f;
  p.surroundGain = 0.70f;
  p.directReject = 0.0f;

  OhlMusicUpmixer unlocked;
  p.frontLock = 0.0f;
  REQUIRE(unlocked.Init(p));
  std::vector<float> unlockedOut(frames * 6, 0.0f);
  unlocked.ProcessStereo(in.data(), frames, unlockedOut.data());

  OhlMusicUpmixer locked;
  p.frontLock = 1.0f;
  REQUIRE(locked.Init(p));
  std::vector<float> lockedOut(frames * 6, 0.0f);
  locked.ProcessStereo(in.data(), frames, lockedOut.data());

  const double unlockedRear =
      0.5 * (ChannelRms(unlockedOut, 4, 512) + ChannelRms(unlockedOut, 5, 512));
  const double lockedRear =
      0.5 * (ChannelRms(lockedOut, 4, 512) + ChannelRms(lockedOut, 5, 512));

  MESSAGE("decorrelated rear RMS unlocked=" << unlockedRear << " locked=" << lockedRear);
  CHECK(unlockedRear > 0.05);
  CHECK(std::fabs(lockedRear - unlockedRear) < unlockedRear * 0.03);
}

TEST_CASE("OHL Music v0.6 preserves rear asymmetry instead of mirroring side energy")
{
  constexpr size_t frames = 4096;
  const auto in = MakeSineStereo(frames, 8000.0, false, false);

  OhlMusicUpmixer upmixer;
  auto p = EqualDistanceParams();
  p.centerTrebleGain = 0.0f;
  p.surroundGain = 0.0f;
  p.widthFloor = 0.25f;
  p.frontLock = 0.90f;
  p.directReject = 0.0f;
  REQUIRE(upmixer.Init(p));

  std::vector<float> out(frames * 6, 0.0f);
  upmixer.ProcessStereo(in.data(), frames, out.data());

  const double left = ChannelRms(out, 4, 512);
  const double right = ChannelRms(out, 5, 512);
  MESSAGE("asymmetric residual rear RMS left=" << left << " right=" << right);

  CHECK(left > 0.03);
  CHECK(right < left * 0.05);
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
