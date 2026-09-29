// OhlMusicUpmixer.cpp — see OhlMusicUpmixer.h.
#include "OhlMusicUpmixer.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kSpeedOfSoundMetresPerSecond = 343.0;
constexpr double kMetresPerInch = 0.0254;
constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1.0e-20;

struct BandStats
{
  double eL = 0.0;
  double eR = 0.0;
  double cross = 0.0;
  double eMid = 0.0;
  double eSide = 0.0;

  void Add(double l, double r)
  {
    const double mid = 0.5 * (l + r);
    const double side = 0.5 * (l - r);
    eL += l * l;
    eR += r * r;
    cross += l * r;
    eMid += mid * mid;
    eSide += side * side;
  }
};

double AmbienceScore(const BandStats& b)
{
  const double denom = std::sqrt(b.eL * b.eR);
  const double corr = denom > kEps ? std::clamp(b.cross / denom, -1.0, 1.0) : 1.0;

  const double rmsL = std::sqrt(b.eL);
  const double rmsR = std::sqrt(b.eR);
  const double sum = rmsL + rmsR;
  const double balance = sum > kEps ? 2.0 * std::min(rmsL, rmsR) / sum : 1.0;

  const double sideFraction = b.eSide / (b.eMid + b.eSide + kEps);
  const double spatial = std::sqrt(std::clamp(sideFraction, 0.0, 1.0));
  const double diffuse = std::clamp(1.0 - std::max(corr, 0.0), 0.0, 1.0);

  // A hard-panned direct source has side energy but terrible L/R balance. A reverberant/diffuse
  // band tends to keep useful energy on both sides while losing positive correlation.
  const double balanceWeight = 0.08 + 0.92 * balance;
  const double diffuseWeight = 0.12 + 0.88 * diffuse;
  return std::clamp(spatial * balanceWeight * diffuseWeight, 0.0, 1.0);
}

} // namespace

void OhlMusicUpmixer::DelayLine::Configure(int samples)
{
  const int n = std::max(samples, 0);
  buffer.assign(static_cast<size_t>(n), 0.0f);
  pos = 0;
}

void OhlMusicUpmixer::DelayLine::Reset()
{
  std::fill(buffer.begin(), buffer.end(), 0.0f);
  pos = 0;
}

float OhlMusicUpmixer::DelayLine::Process(float x)
{
  if (buffer.empty())
    return x;

  const float y = buffer[pos];
  buffer[pos] = x;
  pos = (pos + 1) % buffer.size();
  return y;
}

void OhlMusicUpmixer::OnePoleLowpass::Configure(float hz, int sampleRate)
{
  if (sampleRate <= 0 || hz <= 0.0f)
  {
    alpha = 1.0f;
    return;
  }

  alpha = static_cast<float>(
      1.0 - std::exp(-2.0 * kPi * static_cast<double>(hz) / static_cast<double>(sampleRate)));
  alpha = std::clamp(alpha, 0.0f, 1.0f);
}

void OhlMusicUpmixer::OnePoleLowpass::Reset()
{
  y = 0.0f;
}

float OhlMusicUpmixer::OnePoleLowpass::Process(float x)
{
  y += alpha * (x - y);
  return y;
}

void OhlMusicUpmixer::OnePoleHighpass::Configure(float hz, int sampleRate)
{
  if (sampleRate <= 0 || hz <= 0.0f)
  {
    alpha = 0.0f;
    return;
  }

  const double dt = 1.0 / static_cast<double>(sampleRate);
  const double rc = 1.0 / (2.0 * kPi * static_cast<double>(hz));
  alpha = static_cast<float>(rc / (rc + dt));
}

void OhlMusicUpmixer::OnePoleHighpass::Reset()
{
  x1 = 0.0f;
  y1 = 0.0f;
}

float OhlMusicUpmixer::OnePoleHighpass::Process(float x)
{
  if (alpha <= 0.0f)
    return x;

  const float y = alpha * (y1 + x - x1);
  x1 = x;
  y1 = y;
  return y;
}

void OhlMusicUpmixer::EnvelopeFollower::Configure(float attackMs, float releaseMs, int sampleRate)
{
  const auto coeff = [sampleRate](float ms) {
    if (sampleRate <= 0 || ms <= 0.0f)
      return 0.0f;
    return static_cast<float>(
        std::exp(-1.0 / (0.001 * static_cast<double>(ms) * static_cast<double>(sampleRate))));
  };

  attackCoeff = coeff(attackMs);
  releaseCoeff = coeff(releaseMs);
}

void OhlMusicUpmixer::EnvelopeFollower::Reset()
{
  value = 0.0f;
}

float OhlMusicUpmixer::EnvelopeFollower::Process(float x)
{
  const float v = std::fabs(x);
  const float coeff = v > value ? attackCoeff : releaseCoeff;
  value = coeff * value + (1.0f - coeff) * v;
  return value;
}

bool OhlMusicUpmixer::Init(const Params& params)
{
  if (params.sampleRate <= 0 ||
      !std::isfinite(params.surroundGain) || params.surroundGain < 0.0f ||
      !std::isfinite(params.widthFloor) || params.widthFloor < 0.0f ||
      !std::isfinite(params.directReject) || params.directReject < 0.0f ||
      params.directReject > 1.0f ||
      !std::isfinite(params.centerTrebleGain) || params.centerTrebleGain < 0.0f ||
      !std::isfinite(params.centerTrebleHz) || params.centerTrebleHz < 0.0f ||
      !std::isfinite(params.rearHighpassHz) || params.rearHighpassHz < 0.0f)
    return false;

  params_ = params;

  float farthest = 0.0f;
  for (float d : params_.distanceInches)
  {
    if (!std::isfinite(d) || d < 0.0f)
      return false;
    farthest = std::max(farthest, d);
  }

  for (int ch = 0; ch < kChannels; ++ch)
  {
    const double extraDistanceMetres =
        static_cast<double>(farthest - params_.distanceInches[static_cast<size_t>(ch)]) *
        kMetresPerInch;
    const double delaySeconds = extraDistanceMetres / kSpeedOfSoundMetresPerSecond;
    delaySamples_[static_cast<size_t>(ch)] =
        static_cast<int>(std::lround(delaySeconds * static_cast<double>(params_.sampleRate)));
    delays_[static_cast<size_t>(ch)].Configure(delaySamples_[static_cast<size_t>(ch)]);
  }

  // Three broad analysis bands: <300 Hz, ~300-3000 Hz, >3000 Hz.
  analysisLowL_.Configure(300.0f, params_.sampleRate);
  analysisLowR_.Configure(300.0f, params_.sampleRate);
  analysisMidL_.Configure(3000.0f, params_.sampleRate);
  analysisMidR_.Configure(3000.0f, params_.sampleRate);

  rearHpL_.Configure(params_.rearHighpassHz, params_.sampleRate);
  rearHpR_.Configure(params_.rearHighpassHz, params_.sampleRate);
  centerHp_.Configure(params_.centerTrebleHz, params_.sampleRate);

  // Local onset detector: fast follows the attack, slow estimates the surrounding programme.
  // It changes rear gain sample-by-sample, never the entire 32 ms AC3 packet.
  eventFast_.Configure(0.6f, 18.0f, params_.sampleRate);
  eventSlow_.Configure(28.0f, 220.0f, params_.sampleRate);

  Reset();
  return true;
}

void OhlMusicUpmixer::Reset()
{
  for (auto& d : delays_)
    d.Reset();

  analysisLowL_.Reset();
  analysisLowR_.Reset();
  analysisMidL_.Reset();
  analysisMidR_.Reset();
  rearHpL_.Reset();
  rearHpR_.Reset();
  centerHp_.Reset();
  eventFast_.Reset();
  eventSlow_.Reset();

  gainInitialized_ = false;
  surroundAmount_ = 0.0f;
  lastCorrelation_ = 1.0f;
}

void OhlMusicUpmixer::ProcessStereo(const float* stereo, size_t frames, float* out51)
{
  if (!stereo || !out51 || frames == 0)
    return;

  BandStats low;
  BandStats mid;
  BandStats high;
  BandStats full;

  for (size_t i = 0; i < frames; ++i)
  {
    const float l = stereo[2 * i];
    const float r = stereo[2 * i + 1];

    const float lowL = analysisLowL_.Process(l);
    const float lowR = analysisLowR_.Process(r);
    const float to3kL = analysisMidL_.Process(l);
    const float to3kR = analysisMidR_.Process(r);
    const float midL = to3kL - lowL;
    const float midR = to3kR - lowR;
    const float highL = l - to3kL;
    const float highR = r - to3kR;

    low.Add(lowL, lowR);
    mid.Add(midL, midR);
    high.Add(highL, highR);
    full.Add(l, r);
  }

  const double fullDenom = std::sqrt(full.eL * full.eR);
  const double fullCorr =
      fullDenom > kEps ? std::clamp(full.cross / fullDenom, -1.0, 1.0) : 1.0;
  lastCorrelation_ = static_cast<float>(fullCorr);

  // Bass contributes little to rear steering. Mid/high diffuseness dominates because that is
  // where room tone, doubled parts, stereo effects and reverberant tails usually live.
  const double ambience =
      0.08 * AmbienceScore(low) +
      0.46 * AmbienceScore(mid) +
      0.46 * AmbienceScore(high);

  float target = static_cast<float>(params_.surroundGain * ambience);
  target = std::clamp(target, 0.0f, params_.surroundGain);

  if (!gainInitialized_)
  {
    surroundAmount_ = target;
    gainInitialized_ = true;
  }
  else
  {
    const float alpha = target > surroundAmount_ ? 0.28f : 0.06f;
    surroundAmount_ += alpha * (target - surroundAmount_);
  }

  const double rmsL = std::sqrt(full.eL);
  const double rmsR = std::sqrt(full.eR);
  const double sum = rmsL + rmsR;
  const double balance = sum > kEps ? 2.0 * std::min(rmsL, rmsR) / sum : 1.0;
  const float centerConfidence =
      static_cast<float>(std::clamp((fullCorr - 0.35) / 0.65, 0.0, 1.0) * balance);

  for (size_t i = 0; i < frames; ++i)
  {
    const float l = stereo[2 * i];
    const float r = stereo[2 * i + 1];
    const float midSample = 0.5f * (l + r);
    const float side = 0.5f * (l - r);

    // Detect a local direct event from the full programme envelope. This is deliberately
    // independent of the ambience analyser: a clap can be stereo/side-heavy yet still be a direct
    // event we do not want to localize behind the listener.
    const float eventSignal = std::max(std::fabs(l), std::fabs(r));
    const float fast = eventFast_.Process(eventSignal);
    const float slow = eventSlow_.Process(eventSignal);
    const float ratio = fast / (slow + 1.0e-5f);
    const float onset = std::clamp((ratio - 1.45f) / 2.2f, 0.0f, 1.0f);
    const float directGain = 1.0f - params_.directReject * onset;

    // v0.3 width is side-only. No common/mono programme is copied into the rear speakers.
    // The base side feed prevents subtle stereo ambience from disappearing completely; the
    // adaptive term opens further when the multiband analyser sees genuinely diffuse material.
    const float rearGain = (params_.widthFloor + surroundAmount_) * directGain;
    float rearL = rearHpL_.Process(side * rearGain);
    float rearR = rearHpR_.Process(-side * rearGain);

    const float center =
        params_.centerTrebleGain * centerConfidence * centerHp_.Process(midSample);

    const float raw[kChannels] = {
        l,
        r,
        center,
        0.0f,
        std::clamp(rearL, -1.0f, 1.0f),
        std::clamp(rearR, -1.0f, 1.0f),
    };

    for (int ch = 0; ch < kChannels; ++ch)
      out51[kChannels * i + static_cast<size_t>(ch)] =
          delays_[static_cast<size_t>(ch)].Process(raw[ch]);
  }
}
