#include "doctest.h"
#include "OhlSpatialAnalyzer.h"

#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> MakeStereo(size_t frames,
                              double commonHz,
                              float commonAmp,
                              double sideHz,
                              float sideAmp,
                              bool hardLeft = false)
{
  std::vector<float> x(frames * 2, 0.0f);
  for (size_t i = 0; i < frames; ++i)
  {
    const double t = static_cast<double>(i) / 48000.0;
    const float common =
        commonAmp * static_cast<float>(std::sin(2.0 * kPi * commonHz * t));
    const float side =
        sideAmp * static_cast<float>(std::sin(2.0 * kPi * sideHz * t + 0.37));

    if (hardLeft)
    {
      x[2 * i] = common + side;
      x[2 * i + 1] = 0.0f;
    }
    else
    {
      x[2 * i] = common + side;
      x[2 * i + 1] = common - side;
    }
  }
  return x;
}

OhlSpatialAnalyzer MakeAnalyzer(float threshold = 0.30f)
{
  OhlSpatialAnalyzer a;
  OhlSpatialAnalyzer::Params p;
  p.sampleRate = 48000;
  p.fftSize = 512;
  p.hopSize = 256;
  p.spatialBinThreshold = threshold;
  REQUIRE(a.Init(p));
  return a;
}

} // namespace

TEST_CASE("Spectral spatial analyzer classifies centered mono as front content")
{
  auto analyzer = MakeAnalyzer();
  const auto in = MakeStereo(4096, 1200.0, 0.30f, 3000.0, 0.0f);
  const auto m = analyzer.Analyze(in.data(), 4096);

  MESSAGE("mono ambience=" << m.ambience
          << " center=" << m.center
          << " bins=" << m.spatialBinFraction);
  CHECK(m.center > 0.85f);
  CHECK(m.ambience < 0.08f);
  CHECK(m.spatialBinFraction < 0.08f);
}

TEST_CASE("Spectral spatial analyzer finds quiet high-frequency ambience behind a loud center")
{
  auto analyzer = MakeAnalyzer();
  const auto dry = MakeStereo(4096, 1000.0, 0.32f, 7800.0, 0.0f);
  const auto spatial = MakeStereo(4096, 1000.0, 0.32f, 7800.0, 0.055f);

  const auto dryM = analyzer.Analyze(dry.data(), 4096);
  analyzer.Reset();
  const auto spatialM = analyzer.Analyze(spatial.data(), 4096);

  MESSAGE("dry ambience=" << dryM.ambience
          << " spatial ambience=" << spatialM.ambience
          << " spatial bins=" << spatialM.spatialBinFraction
          << " center=" << spatialM.center);
  CHECK(spatialM.center > 0.45f);
  CHECK(spatialM.ambience > dryM.ambience + 0.12f);
  CHECK(spatialM.spatialBinFraction > dryM.spatialBinFraction + 0.05f);
}

TEST_CASE("Spectral spatial analyzer distinguishes hard pan from balanced diffuse side information")
{
  auto hardPanAnalyzer = MakeAnalyzer();
  const auto hardPan = MakeStereo(4096, 1800.0, 0.26f, 0.0, 0.0f, true);
  const auto hard = hardPanAnalyzer.Analyze(hardPan.data(), 4096);

  auto diffuseAnalyzer = MakeAnalyzer();
  const auto diffuse = MakeStereo(4096, 0.0, 0.0f, 4200.0, 0.26f);
  const auto wide = diffuseAnalyzer.Analyze(diffuse.data(), 4096);

  MESSAGE("hardPan score=" << hard.hardPan << " ambience=" << hard.ambience
          << " diffuse hardPan=" << wide.hardPan << " ambience=" << wide.ambience);
  CHECK(hard.hardPan > wide.hardPan + 0.35f);
  CHECK(wide.ambience > hard.ambience + 0.20f);
}

TEST_CASE("Spectral spatial analyzer keeps directionality for genuinely spatial cues")
{
  OhlSpatialAnalyzer analyzer;
  OhlSpatialAnalyzer::Params p;
  p.sampleRate = 48000;
  p.fftSize = 512;
  p.hopSize = 256;
  p.spatialBinThreshold = 0.15f;
  REQUIRE(analyzer.Init(p));

  std::vector<float> in(4096 * 2, 0.0f);
  for (size_t i = 0; i < 4096; ++i)
  {
    const double tt = static_cast<double>(i) / 48000.0;
    const float center =
        0.08f * static_cast<float>(std::sin(2.0 * kPi * 900.0 * tt));
    const float leftSpatial =
        static_cast<float>(std::sin(2.0 * kPi * 6300.0 * tt + 0.25));
    const float rightSpatial =
        static_cast<float>(std::sin(2.0 * kPi * 7600.0 * tt + 1.1));

    // Both cues exist in both channels, so neither is a hard pan. The 6.3 kHz cue is
    // substantially stronger on L while the smaller 7.6 kHz cue leans R.
    in[2 * i] =
        center + 0.18f * leftSpatial - 0.035f * rightSpatial;
    in[2 * i + 1] =
        center - 0.085f * leftSpatial + 0.055f * rightSpatial;
  }

  const auto m = analyzer.Analyze(in.data(), 4096);
  MESSAGE("spatial shares L=" << m.spatialLeftShare << " R=" << m.spatialRightShare
          << " ambience=" << m.ambience << " bins=" << m.spatialBinFraction);

  CHECK(m.ambience > 0.20f);
  CHECK(m.spatialBinFraction > 0.10f);
  CHECK(m.spatialLeftShare > 0.58f);
  CHECK(m.spatialRightShare < 0.42f);
}
