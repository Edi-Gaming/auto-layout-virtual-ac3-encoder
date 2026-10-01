#include "doctest.h"
#include "OhlSpectralRouter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> AntiPhaseSine(size_t frames, double hz, float amp = 0.30f)
{
  std::vector<float> x(frames * 2, 0.0f);
  for (size_t i = 0; i < frames; ++i)
  {
    const double t = static_cast<double>(i) / 48000.0;
    const float s = amp * static_cast<float>(std::sin(2.0 * kPi * hz * t));
    x[2 * i] = s;
    x[2 * i + 1] = -s;
  }
  return x;
}

double Rms(const std::vector<float>& x, size_t skip = 0)
{
  if (skip >= x.size()) return 0.0;
  double e = 0.0;
  for (size_t i = skip; i < x.size(); ++i)
    e += static_cast<double>(x[i]) * x[i];
  return std::sqrt(e / static_cast<double>(x.size() - skip));
}

OhlSpectralRouter::Params BaseParams()
{
  OhlSpectralRouter::Params p;
  p.sampleRate = 48000;
  p.fftSize = 512;
  p.hopSize = 256;
  p.spatialThreshold = 0.20f;
  p.acquireMs = 25.0f;
  p.releaseMs = 180.0f;
  p.surroundGain = 0.75f;
  p.widthFloor = 0.12f;
  p.dimension = 0.0f;
  p.globalFrontLock = 0.0f;
  p.directReject = 0.0f;
  return p;
}

void Run(OhlSpectralRouter& router,
         const std::vector<float>& stereo,
         std::vector<float>& l,
         std::vector<float>& r)
{
  const size_t frames = stereo.size() / 2;
  l.assign(frames, 0.0f);
  r.assign(frames, 0.0f);
  router.Process(stereo.data(), frames, l.data(), r.data());
}

} // namespace

TEST_CASE("v0.11 spectral router has an explicit 512-sample causal latency")
{
  auto p = BaseParams();
  p.spatialThreshold = 0.0f;
  p.surroundGain = 0.0f;
  p.widthFloor = 1.0f;
  p.steering = {{1.0f, 1.0f, 1.0f, 1.0f}};

  OhlSpectralRouter router;
  REQUIRE(router.Init(p));
  CHECK(router.LatencySamples() == 512);

  constexpr size_t frames = 2048;
  std::vector<float> in(frames * 2, 0.0f);
  in[0] = 1.0f;
  in[1] = -1.0f;

  std::vector<float> l, r;
  Run(router, in, l, r);

  double prePeak = 0.0;
  for (size_t i = 0; i < 512; ++i)
    prePeak = std::max(prePeak, std::fabs(static_cast<double>(l[i])));

  double postPeak = 0.0;
  for (size_t i = 512; i < 1024; ++i)
    postPeak = std::max(postPeak, std::fabs(static_cast<double>(l[i])));

  MESSAGE("router pre-latency peak=" << prePeak << " post=" << postPeak);
  CHECK(prePeak < 1.0e-7);
  CHECK(postPeak > 0.01);
}

TEST_CASE("v0.11 spectral router produces no rear content from exact mono")
{
  auto p = BaseParams();
  p.widthFloor = 0.30f;

  OhlSpectralRouter router;
  REQUIRE(router.Init(p));

  constexpr size_t frames = 8192;
  std::vector<float> in(frames * 2, 0.0f);
  for (size_t i = 0; i < frames; ++i)
  {
    const double t = static_cast<double>(i) / 48000.0;
    const float s = 0.35f * static_cast<float>(std::sin(2.0 * kPi * 1300.0 * t));
    in[2 * i] = s;
    in[2 * i + 1] = s;
  }

  std::vector<float> l, r;
  Run(router, in, l, r);

  CHECK(Rms(l, 1024) < 1.0e-7);
  CHECK(Rms(r, 1024) < 1.0e-7);
}

TEST_CASE("v0.11 spectral router steers only the enabled frequency band")
{
  auto air = BaseParams();
  air.widthFloor = 0.0f;
  air.steering = {{0.0f, 0.0f, 0.0f, 1.0f}};

  OhlSpectralRouter highRouter;
  OhlSpectralRouter bodyRouter;
  REQUIRE(highRouter.Init(air));
  REQUIRE(bodyRouter.Init(air));

  const auto high = AntiPhaseSine(8192, 9000.0);
  const auto body = AntiPhaseSine(8192, 1000.0);
  std::vector<float> hl, hr, bl, br;
  Run(highRouter, high, hl, hr);
  Run(bodyRouter, body, bl, br);

  const double highRms = 0.5 * (Rms(hl, 2048) + Rms(hr, 2048));
  const double bodyRms = 0.5 * (Rms(bl, 2048) + Rms(br, 2048));
  MESSAGE("air-only high RMS=" << highRms << " body RMS=" << bodyRms);

  CHECK(highRms > 0.03);
  CHECK(bodyRms < highRms * 0.03);
}

TEST_CASE("v0.11 Dimension moves the spectral rear field front or rear")
{
  auto front = BaseParams();
  front.dimension = -1.0f;
  auto rear = front;
  rear.dimension = 1.0f;

  OhlSpectralRouter frontRouter;
  OhlSpectralRouter rearRouter;
  REQUIRE(frontRouter.Init(front));
  REQUIRE(rearRouter.Init(rear));

  const auto in = AntiPhaseSine(8192, 6500.0);
  std::vector<float> fl, fr, rl, rr;
  Run(frontRouter, in, fl, fr);
  Run(rearRouter, in, rl, rr);

  const double frontRms = 0.5 * (Rms(fl, 2048) + Rms(fr, 2048));
  const double rearRms = 0.5 * (Rms(rl, 2048) + Rms(rr, 2048));
  MESSAGE("dimension front RMS=" << frontRms << " rear RMS=" << rearRms);

  CHECK(frontRms > 0.005);
  CHECK(rearRms > frontRms * 2.5);
}

TEST_CASE("v0.11 body-band Front Lock suppresses coherent widened body without killing air")
{
  constexpr size_t frames = 8192;
  std::vector<float> body(frames * 2, 0.0f);
  for (size_t i = 0; i < frames; ++i)
  {
    const double t = static_cast<double>(i) / 48000.0;
    const float common = 0.34f * static_cast<float>(std::sin(2.0 * kPi * 1100.0 * t));
    const float width = 0.075f * static_cast<float>(std::sin(2.0 * kPi * 1100.0 * t + 0.05));
    body[2 * i] = common + width;
    body[2 * i + 1] = common - width;
  }

  auto unlocked = BaseParams();
  unlocked.widthFloor = 0.25f;
  unlocked.surroundGain = 0.0f;
  unlocked.globalFrontLock = 1.0f;
  unlocked.frontLock = {{0.0f, 0.0f, 0.0f, 0.0f}};

  auto locked = unlocked;
  locked.frontLock[1] = 1.0f;

  OhlSpectralRouter a;
  OhlSpectralRouter b;
  REQUIRE(a.Init(unlocked));
  REQUIRE(b.Init(locked));

  std::vector<float> al, ar, bl, br;
  Run(a, body, al, ar);
  Run(b, body, bl, br);

  const double open = 0.5 * (Rms(al, 2048) + Rms(ar, 2048));
  const double shut = 0.5 * (Rms(bl, 2048) + Rms(br, 2048));
  MESSAGE("body Front Lock open=" << open << " locked=" << shut);
  CHECK(open > 0.0005);
  CHECK(shut < open * 0.30);

  auto airUnlocked = unlocked;
  auto airLocked = locked;
  const auto air = AntiPhaseSine(frames, 9000.0, 0.20f);
  OhlSpectralRouter c;
  OhlSpectralRouter d;
  REQUIRE(c.Init(airUnlocked));
  REQUIRE(d.Init(airLocked));
  std::vector<float> cl, cr, dl, dr;
  Run(c, air, cl, cr);
  Run(d, air, dl, dr);
  const double airOpen = 0.5 * (Rms(cl, 2048) + Rms(cr, 2048));
  const double airShut = 0.5 * (Rms(dl, 2048) + Rms(dr, 2048));
  CHECK(std::fabs(airOpen - airShut) < airOpen * 0.05);
}

TEST_CASE("v0.11 spectral ownership releases through silence")
{
  auto p = BaseParams();
  p.acquireMs = 15.0f;
  p.releaseMs = 80.0f;

  OhlSpectralRouter router;
  REQUIRE(router.Init(p));

  const auto spatial = AntiPhaseSine(8192, 5200.0);
  std::vector<float> l, r;
  Run(router, spatial, l, r);
  const float owned = router.LastMetrics().meanOwnership;
  MESSAGE("ownership after spatial content=" << owned);
  CHECK(owned > 0.40f);

  std::vector<float> silence(8192 * 2, 0.0f);
  Run(router, silence, l, r);
  const float released = router.LastMetrics().meanOwnership;
  MESSAGE("ownership after silence=" << released);
  CHECK(released < owned * 0.20f);
}
