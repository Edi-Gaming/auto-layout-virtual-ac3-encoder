// OhlMusicUpmixer.cpp — see OhlMusicUpmixer.h.
#include "OhlMusicUpmixer.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kSpeedOfSoundMetresPerSecond = 343.0;
constexpr double kMetresPerInch = 0.0254;

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

bool OhlMusicUpmixer::Init(const Params& params)
{
  if (params.sampleRate <= 0 || !std::isfinite(params.surroundGain) || params.surroundGain < 0.0f)
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

  Reset();
  return true;
}

void OhlMusicUpmixer::Reset()
{
  for (auto& d : delays_)
    d.Reset();
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
  double peak = 0.0;

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
    peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
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

  // Side energy is necessary but not sufficient. The coherence and balance terms keep hard-panned
  // direct sources front-biased while letting decorrelated/anti-phase ambience open into the room.
  const double sideFraction = eSide / (eMid + eSide + eps);
  const double spatial = std::sqrt(std::clamp(sideFraction, 0.0, 1.0));
  const double diffuse = std::clamp(1.0 - std::max(corr, 0.0), 0.0, 1.0);
  const double balanceWeight = 0.20 + 0.80 * balance;
  const double diffuseWeight = 0.30 + 0.70 * diffuse;

  // High crest factor usually means a transient-heavy block. Pull the derived rear bed down
  // slightly so attacks stay anchored to the front image.
  const double combinedRms =
      std::sqrt(0.5 * (eL + eR) / static_cast<double>(frames));
  const double crest = combinedRms > eps ? peak / combinedRms : 1.0;
  const double transientWeight =
      std::clamp(1.15 - 0.07 * std::max(0.0, crest - 2.0), 0.70, 1.0);

  float target = static_cast<float>(
      params_.surroundGain * spatial * balanceWeight * diffuseWeight * transientWeight);
  target = std::clamp(target, 0.0f, params_.surroundGain);

  if (!gainInitialized_)
  {
    surroundAmount_ = target;
    gainInitialized_ = true;
  }
  else
  {
    // Fast enough to open with ambience, slower to collapse so room tone does not pump.
    const float alpha = target > surroundAmount_ ? 0.55f : 0.18f;
    surroundAmount_ += alpha * (target - surroundAmount_);
  }

  for (size_t i = 0; i < frames; ++i)
  {
    const float l = stereo[2 * i];
    const float r = stereo[2 * i + 1];
    const float side = 0.5f * (l - r);
    const float surround = side * surroundAmount_;

    // Phantom-center-first v0.1: preserve fronts exactly, no physical center, no synthesized LFE.
    // Opposite surround polarity deliberately avoids creating a stable rear phantom-center image.
    const float raw[kChannels] = {l, r, 0.0f, 0.0f, surround, -surround};

    for (int ch = 0; ch < kChannels; ++ch)
      out51[kChannels * i + static_cast<size_t>(ch)] =
          delays_[static_cast<size_t>(ch)].Process(raw[ch]);
  }
}
