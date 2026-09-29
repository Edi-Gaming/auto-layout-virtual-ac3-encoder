// OhlMusicUpmixer.cpp — see OhlMusicUpmixer.h.
#include "OhlMusicUpmixer.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kSpeedOfSoundMetresPerSecond = 343.0;
constexpr double kMetresPerInch = 0.0254;
constexpr double kPi = 3.14159265358979323846;

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

void OhlMusicUpmixer::OnePoleAllpass::Configure(float coefficient)
{
  a = std::clamp(coefficient, -0.95f, 0.95f);
}

void OhlMusicUpmixer::OnePoleAllpass::Reset()
{
  x1 = 0.0f;
  y1 = 0.0f;
}

float OhlMusicUpmixer::OnePoleAllpass::Process(float x)
{
  const float y = -a * x + x1 + a * y1;
  x1 = x;
  y1 = y;
  return y;
}

bool OhlMusicUpmixer::Init(const Params& params)
{
  if (params.sampleRate <= 0 ||
      !std::isfinite(params.surroundGain) || params.surroundGain < 0.0f ||
      !std::isfinite(params.widthFloor) || params.widthFloor < 0.0f ||
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

  rearHpL_.Configure(params_.rearHighpassHz, params_.sampleRate);
  rearHpR_.Configure(params_.rearHighpassHz, params_.sampleRate);
  centerHp_.Configure(params_.centerTrebleHz, params_.sampleRate);

  // Deliberately different phase rotations left/right. These are all-pass networks: they change
  // phase but not steady-state magnitude, widening the rear field without audible echo.
  rearApL_.Configure(0.55f);
  rearApR_.Configure(-0.37f);

  Reset();
  return true;
}

void OhlMusicUpmixer::Reset()
{
  for (auto& d : delays_)
    d.Reset();

  rearHpL_.Reset();
  rearHpR_.Reset();
  centerHp_.Reset();
  rearApL_.Reset();
  rearApR_.Reset();

  gainInitialized_ = false;
  surroundAmount_ = 0.0f;
  lastCorrelation_ = 1.0f;
}

void OhlMusicUpmixer::ProcessStereo(const float* stereo, size_t frames, float* out51)
{
  if (!stereo || !out51 || frames == 0)
    return;

  double eL = 0.0;
  double eR = 0.0;
  double cross = 0.0;
  double eMid = 0.0;
  double eSide = 0.0;

  for (size_t i = 0; i < frames; ++i)
  {
    const double l = stereo[2 * i];
    const double r = stereo[2 * i + 1];
    const double mid = 0.5 * (l + r);
    const double side = 0.5 * (l - r);

    eL += l * l;
    eR += r * r;
    cross += l * r;
    eMid += mid * mid;
    eSide += side * side;
  }

  constexpr double eps = 1.0e-20;
  const double denom = std::sqrt(eL * eR);
  double corr = denom > eps ? cross / denom : 1.0;
  corr = std::clamp(corr, -1.0, 1.0);
  lastCorrelation_ = static_cast<float>(corr);

  const double rmsL = std::sqrt(eL / static_cast<double>(frames));
  const double rmsR = std::sqrt(eR / static_cast<double>(frames));
  const double sumRms = rmsL + rmsR;
  const double balance = sumRms > eps ? 2.0 * std::min(rmsL, rmsR) / sumRms : 1.0;

  const double sideFraction = eSide / (eMid + eSide + eps);
  const double spatial = std::sqrt(std::clamp(sideFraction, 0.0, 1.0));
  const double diffuse = std::clamp(1.0 - std::max(corr, 0.0), 0.0, 1.0);

  // v0.2 deliberately has NO transient gate. The permanent width bed below must survive a snare
  // hit, and the adaptive ambience term is allowed to stay alive through transient-heavy blocks.
  const double balanceWeight = 0.25 + 0.75 * balance;
  const double diffuseWeight = 0.30 + 0.70 * diffuse;

  float target = static_cast<float>(
      params_.surroundGain * spatial * balanceWeight * diffuseWeight);
  target = std::clamp(target, 0.0f, params_.surroundGain);

  if (!gainInitialized_)
  {
    surroundAmount_ = target;
    gainInitialized_ = true;
  }
  else
  {
    // Open reasonably quickly, decay slowly (~many 32 ms AC3 blocks) so the rear field does not
    // audibly pump between musical events.
    const float alpha = target > surroundAmount_ ? 0.34f : 0.045f;
    surroundAmount_ += alpha * (target - surroundAmount_);
  }

  // Only genuinely centered material should use the bright physical center.
  const float centerConfidence =
      static_cast<float>(std::clamp((corr - 0.35) / 0.65, 0.0, 1.0) * balance);

  for (size_t i = 0; i < frames; ++i)
  {
    const float l = stereo[2 * i];
    const float r = stereo[2 * i + 1];
    const float mid = 0.5f * (l + r);
    const float side = 0.5f * (l - r);

    // Permanent width bed: remove most common-mode center information, but intentionally leave
    // some same-side program so even highly correlated modern mixes still feel wider than FL/FR.
    const float edgeL = l - 0.75f * mid;
    const float edgeR = r - 0.75f * mid;

    float rearL = params_.widthFloor * edgeL + surroundAmount_ * side;
    float rearR = params_.widthFloor * edgeR - surroundAmount_ * side;

    rearL = rearApL_.Process(rearHpL_.Process(rearL));
    rearR = rearApR_.Process(rearHpR_.Process(rearR));

    // Use the Bose center only as a bright high-frequency accent. The fundamental vocal/body
    // remains phantom-centered in FL/FR.
    const float center =
        params_.centerTrebleGain * centerConfidence * centerHp_.Process(mid);

    const float raw[kChannels] = {
        l,
        r,
        center,
        0.0f, // no synthesized LFE in v0.2
        std::clamp(rearL, -1.0f, 1.0f),
        std::clamp(rearR, -1.0f, 1.0f),
    };

    for (int ch = 0; ch < kChannels; ++ch)
      out51[kChannels * i + static_cast<size_t>(ch)] =
          delays_[static_cast<size_t>(ch)].Process(raw[ch]);
  }
}
