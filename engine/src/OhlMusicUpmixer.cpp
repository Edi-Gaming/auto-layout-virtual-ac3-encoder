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

double BandCorrelation(const BandStats& b)
{
  const double denom = std::sqrt(b.eL * b.eR);
  return denom > kEps ? std::clamp(b.cross / denom, -1.0, 1.0) : 1.0;
}

double BandBalance(const BandStats& b)
{
  const double rmsL = std::sqrt(b.eL);
  const double rmsR = std::sqrt(b.eR);
  const double sum = rmsL + rmsR;
  return sum > kEps ? 2.0 * std::min(rmsL, rmsR) / sum : 1.0;
}

double CenterConfidence(const BandStats& b)
{
  const double corr = BandCorrelation(b);
  const double balance = BandBalance(b);
  const double positiveCorr = std::clamp((corr - 0.25) / 0.75, 0.0, 1.0);
  return std::clamp(positiveCorr * balance, 0.0, 1.0);
}

double AmbienceScore(const BandStats& b)
{
  const double corr = BandCorrelation(b);
  const double balance = BandBalance(b);

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

void OhlMusicUpmixer::Allpass1::Configure(float coefficient)
{
  a = std::clamp(coefficient, -0.95f, 0.95f);
}

void OhlMusicUpmixer::Allpass1::Reset()
{
  x1 = 0.0f;
  y1 = 0.0f;
}

float OhlMusicUpmixer::Allpass1::Process(float x)
{
  // First-order all-pass: flat magnitude response, frequency-dependent phase only.
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
      !std::isfinite(params.ambienceLowWeight) || params.ambienceLowWeight < 0.0f ||
      !std::isfinite(params.ambienceMidWeight) || params.ambienceMidWeight < 0.0f ||
      !std::isfinite(params.ambienceHighWeight) || params.ambienceHighWeight < 0.0f ||
      !std::isfinite(params.ambienceAttackMs) || params.ambienceAttackMs <= 0.0f ||
      !std::isfinite(params.ambienceReleaseMs) || params.ambienceReleaseMs <= 0.0f ||
      !std::isfinite(params.diffuseThreshold) || params.diffuseThreshold < 0.0f ||
      params.diffuseThreshold >= 1.0f ||
      !std::isfinite(params.spectralIntelligence) || params.spectralIntelligence < 0.0f ||
      params.spectralIntelligence > 1.0f ||
      !std::isfinite(params.spatialBinThreshold) || params.spatialBinThreshold < 0.0f ||
      params.spatialBinThreshold > 1.0f ||
      !std::isfinite(params.perBinRouting) || params.perBinRouting < 0.0f ||
      params.perBinRouting > 1.0f ||
      !std::isfinite(params.spectralAcquireMs) || params.spectralAcquireMs <= 0.0f ||
      !std::isfinite(params.spectralReleaseMs) || params.spectralReleaseMs <= 0.0f ||
      !std::isfinite(params.dimension) || params.dimension < -1.0f || params.dimension > 1.0f ||
      !std::isfinite(params.centerWidth) || params.centerWidth < 0.0f || params.centerWidth > 1.0f ||
      !std::isfinite(params.frontLock) || params.frontLock < 0.0f || params.frontLock > 1.0f ||
      !std::isfinite(params.rearBudget) || params.rearBudget <= 0.0f ||
      params.rearBudget > 1.0f ||
      !std::isfinite(params.directReject) || params.directReject < 0.0f ||
      params.directReject > 1.0f ||
      !std::isfinite(params.directThreshold) || params.directThreshold <= 1.0f ||
      !std::isfinite(params.directRecoveryMs) || params.directRecoveryMs <= 0.0f ||
      !std::isfinite(params.centerTrebleGain) || params.centerTrebleGain < 0.0f ||
      !std::isfinite(params.centerTrebleHz) || params.centerTrebleHz < 0.0f ||
      !std::isfinite(params.centerLowpassHz) || params.centerLowpassHz <= 0.0f ||
      !std::isfinite(params.rearHighpassHz) || params.rearHighpassHz < 0.0f ||
      !std::isfinite(params.rearLowpassHz) || params.rearLowpassHz <= 0.0f ||
      !std::isfinite(params.rearLeftTrim) || params.rearLeftTrim < 0.0f ||
      !std::isfinite(params.rearRightTrim) || params.rearRightTrim < 0.0f)
    return false;

  params_ = params;

  for (float v : params_.spectralSteering)
    if (!std::isfinite(v) || v < 0.0f || v > 4.0f)
      return false;
  for (float v : params_.spectralFrontLock)
    if (!std::isfinite(v) || v < 0.0f || v > 2.0f)
      return false;

  OhlSpatialAnalyzer::Params analyzerParams;
  analyzerParams.sampleRate = params_.sampleRate;
  analyzerParams.fftSize = 512;
  analyzerParams.hopSize = 256;
  analyzerParams.spatialBinThreshold = params_.spatialBinThreshold;
  if (!spatialAnalyzer_.Init(analyzerParams))
    return false;

  OhlSpectralRouter::Params routerParams;
  routerParams.sampleRate = params_.sampleRate;
  routerParams.fftSize = 512;
  routerParams.hopSize = 256;
  routerParams.spatialThreshold = params_.spatialBinThreshold;
  routerParams.acquireMs = params_.spectralAcquireMs;
  routerParams.releaseMs = params_.spectralReleaseMs;
  routerParams.surroundGain = params_.surroundGain;
  routerParams.widthFloor = params_.widthFloor;
  routerParams.dimension = params_.dimension;
  routerParams.steering = params_.spectralSteering;
  routerParams.frontLock = params_.spectralFrontLock;
  routerParams.globalFrontLock = params_.frontLock;
  routerParams.directReject = params_.directReject;
  if (!spectralRouter_.Init(routerParams))
    return false;
  processingLatencySamples_ = spectralRouter_.LatencySamples();
  broadRearDelayL_.Configure(processingLatencySamples_);
  broadRearDelayR_.Configure(processingLatencySamples_);

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
    const int spectralCompensation = ch < 4 ? processingLatencySamples_ : 0;
    delays_[static_cast<size_t>(ch)].Configure(
        delaySamples_[static_cast<size_t>(ch)] + spectralCompensation);
  }

  // Three broad analysis bands: <300 Hz, ~300-3000 Hz, >3000 Hz.
  analysisLowL_.Configure(300.0f, params_.sampleRate);
  analysisLowR_.Configure(300.0f, params_.sampleRate);
  analysisMidL_.Configure(3000.0f, params_.sampleRate);
  analysisMidR_.Configure(3000.0f, params_.sampleRate);

  rearHpL_.Configure(params_.rearHighpassHz, params_.sampleRate);
  rearHpR_.Configure(params_.rearHighpassHz, params_.sampleRate);
  rearLpL_.Configure(params_.rearLowpassHz, params_.sampleRate);
  rearLpR_.Configure(params_.rearLowpassHz, params_.sampleRate);

  // Two intentionally different short phase networks. They are all-pass, so they do not alter
  // magnitude or create amplitude modulation; they only stop SL/SR from being coherent copies.
  rearDecorL_[0].Configure(0.43f);
  rearDecorL_[1].Configure(-0.61f);
  rearDecorR_[0].Configure(-0.37f);
  rearDecorR_[1].Configure(0.69f);

  centerHp_.Configure(params_.centerTrebleHz, params_.sampleRate);
  centerLp_.Configure(params_.centerLowpassHz, params_.sampleRate);

  // Local onset detector: fast follows the attack, slow estimates the surrounding programme.
  // The fast release is user-tunable so clap/snare rejection can be made tighter or softer.
  eventFast_.Configure(0.6f, params_.directRecoveryMs, params_.sampleRate);
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
  rearLpL_.Reset();
  rearLpR_.Reset();
  for (auto& ap : rearDecorL_) ap.Reset();
  for (auto& ap : rearDecorR_) ap.Reset();
  centerHp_.Reset();
  centerLp_.Reset();
  eventFast_.Reset();
  eventSlow_.Reset();
  spatialAnalyzer_.Reset();
  spectralRouter_.Reset();
  broadRearDelayL_.Reset();
  broadRearDelayR_.Reset();

  gainInitialized_ = false;
  surroundAmount_ = 0.0f;
  lastCorrelation_ = 1.0f;
  lastFrontLockConfidence_ = 0.0f;
  lastSpectralAmbience_ = 0.0f;
  lastSpatialBinFraction_ = 0.0f;
  lastSpectralCenter_ = 0.0f;
  lastSpectralTransient_ = 0.0f;
  lastBandOwnership_ = {{0, 0, 0, 0}};
  lastBandCenter_ = {{0, 0, 0, 0}};
  lastRearBudgetScale_ = 1.0f;
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

  const double fullCorr = BandCorrelation(full);
  lastCorrelation_ = static_cast<float>(fullCorr);

  // Legacy broad-band estimate remains as a stable fallback, but v0.10's primary recognizer is
  // an overlapping 512-point STFT classifier. The spectral path can notice a relatively small
  // set of spatial/reverberant bins even when a loud centered vocal or instrument dominates the
  // same broad frequency region.
  const double broadAmbience = std::clamp(
      static_cast<double>(params_.ambienceLowWeight) * AmbienceScore(low) +
      static_cast<double>(params_.ambienceMidWeight) * AmbienceScore(mid) +
      static_cast<double>(params_.ambienceHighWeight) * AmbienceScore(high),
      0.0, 1.0);

  const auto spectral = spatialAnalyzer_.Analyze(stereo, frames);
  lastSpectralAmbience_ = spectral.ambience;
  lastSpatialBinFraction_ = spectral.spatialBinFraction;
  lastSpectralCenter_ = spectral.center;
  lastSpectralTransient_ = spectral.transient;

  const double intelligence =
      std::clamp(static_cast<double>(params_.spectralIntelligence), 0.0, 1.0);
  const double spectralAmbience = std::clamp(
      static_cast<double>(spectral.ambience) *
          (1.0 - 0.18 * static_cast<double>(spectral.transient)),
      0.0, 1.0);
  const double ambience = std::clamp(
      (1.0 - intelligence) * broadAmbience + intelligence * spectralAmbience,
      0.0, 1.0);

  const double gateNorm = std::clamp(
      (ambience - static_cast<double>(params_.diffuseThreshold)) /
          (1.0 - static_cast<double>(params_.diffuseThreshold)),
      0.0, 1.0);
  // v0.7 squared this gate, which made ordinary music almost never open the surrounds.
  // v0.8 uses a square-root law: genuinely diffuse material still reaches full scale, but modest
  // stereo ambience becomes audible instead of being numerically crushed.
  const double diffuseOpen = std::sqrt(gateNorm);
  float target = static_cast<float>(params_.surroundGain * diffuseOpen);
  target = std::clamp(target, 0.0f, params_.surroundGain);

  if (!gainInitialized_)
  {
    surroundAmount_ = target;
    gainInitialized_ = true;
  }
  else
  {
    const double packetMs =
        1000.0 * static_cast<double>(frames) / static_cast<double>(params_.sampleRate);
    const double tauMs = target > surroundAmount_
        ? static_cast<double>(params_.ambienceAttackMs)
        : static_cast<double>(params_.ambienceReleaseMs);
    const float alpha = static_cast<float>(1.0 - std::exp(-packetMs / tauMs));
    surroundAmount_ += std::clamp(alpha, 0.0f, 1.0f) * (target - surroundAmount_);
  }

  const double rmsL = std::sqrt(full.eL);
  const double rmsR = std::sqrt(full.eR);
  const double sum = rmsL + rmsR;
  const double balance = sum > kEps ? 2.0 * std::min(rmsL, rmsR) / sum : 1.0;
  const double leftShare = sum > kEps ? rmsL / sum : 0.5;
  const double rightShare = sum > kEps ? rmsR / sum : 0.5;
  const float centerConfidence =
      static_cast<float>(std::clamp((fullCorr - 0.35) / 0.65, 0.0, 1.0) * balance);
  const bool blockNearlyMono = fullCorr > 0.9995 && balance > 0.995;

  // v0.10 front-lock confidence blends the old broad classifier with the spectral estimate.
  // The spectral term is dominant so a coherent vocal can remain front-anchored even while other
  // frequency bins in the same packet are allowed to drive surround ambience.
  const double broadFrontLock = std::clamp(
      0.15 * CenterConfidence(full) + 0.85 * CenterConfidence(mid), 0.0, 1.0);
  const double frontLockConfidence = std::clamp(
      (1.0 - intelligence) * broadFrontLock +
          intelligence * static_cast<double>(spectral.center),
      0.0, 1.0);
  lastFrontLockConfidence_ = static_cast<float>(frontLockConfidence);
  const float frontLockGain = static_cast<float>(
      1.0 - static_cast<double>(params_.frontLock) * frontLockConfidence);

  rearScratchL_.assign(frames, 0.0f);
  rearScratchR_.assign(frames, 0.0f);
  double rearEnergy = 0.0;

  for (size_t i = 0; i < frames; ++i)
  {
    const float l = stereo[2 * i];
    const float r = stereo[2 * i + 1];
    // Detect a local direct event from the full programme envelope. This is deliberately
    // independent of the ambience analyser: a clap can be stereo/side-heavy yet still be a direct
    // event we do not want to localize behind the listener.
    const float eventSignal = std::max(std::fabs(l), std::fabs(r));
    const float fast = eventFast_.Process(eventSignal);
    const float slow = eventSlow_.Process(eventSignal);
    const float ratio = fast / (slow + 1.0e-5f);
    const float onset =
        std::clamp((ratio - params_.directThreshold) / 2.2f, 0.0f, 1.0f);
    // v0.9: strictly linear rear audio path.
    //
    // The side signal is exactly zero for true mono/center, so there is no need for nonlinear
    // sample-wise "shared content" subtraction. Two different all-pass networks then turn that
    // side information into a less-coherent SL/SR field without changing its magnitude spectrum.
    float side = 0.5f * (l - r);
    if (blockNearlyMono)
      side = 0.0f;

    float decorL = side;
    float decorR = -side;
    for (auto& ap : rearDecorL_) decorL = ap.Process(decorL);
    for (auto& ap : rearDecorR_) decorR = ap.Process(decorR);

    const float open = static_cast<float>(diffuseOpen);

    // The always-on bed is no longer truly "always on". Spectral occupancy decides whether the
    // recording has enough spatially interesting bins to deserve a PLII-like width bed. A dry,
    // front-heavy mix therefore stays more front-biased, while a mix with only a handful of
    // meaningful ambience bins still gets room because occupancy uses sqrt weighting.
    const float occupancy = std::sqrt(std::clamp(spectral.spatialBinFraction, 0.0f, 1.0f));
    const float bedPresence =
        static_cast<float>((1.0 - intelligence) +
                           intelligence * (0.28 + 0.72 * occupancy));
    const float bedGain =
        params_.widthFloor * (0.55f + 0.45f * open) * bedPresence * frontLockGain;

    // Diffuse material gets a stronger layer. We retain most of this when a coherent center exists,
    // because reverb/room tails around a lead vocal are desirable even while the lead stays front.
    const float diffuseLockGain = static_cast<float>(
        1.0 - 0.30 * static_cast<double>(params_.frontLock) * frontLockConfidence);
    const float spreadGain =
        0.75f * surroundAmount_ * diffuseLockGain;

    // Spatially active FFT bins get first say in rear directionality. Fall back toward the
    // broad packet energy shares as intelligence is reduced.
    const double directionEvidence = std::clamp(
        4.0 * static_cast<double>(spectral.spatialBinFraction), 0.0, 1.0);
    const double directionIntelligence = intelligence * directionEvidence;
    const double smartLeftShare =
        (1.0 - directionIntelligence) * leftShare +
        directionIntelligence * static_cast<double>(spectral.spatialLeftShare);
    const double smartRightShare =
        (1.0 - directionIntelligence) * rightShare +
        directionIntelligence * static_cast<double>(spectral.spatialRightShare);
    const float leftBias = static_cast<float>(
        0.10 + 0.90 * std::clamp(2.0 * smartLeftShare, 0.0, 1.0));
    const float rightBias = static_cast<float>(
        0.10 + 0.90 * std::clamp(2.0 * smartRightShare, 0.0, 1.0));

    // Direct-event protection ducks attacks but cannot collapse the rear bed.
    const float effectiveReject = 0.45f * params_.directReject;
    const float softenedDirectGain = 1.0f - effectiveReject * onset;

    float rearL = rearLpL_.Process(
        rearHpL_.Process(decorL * (bedGain + spreadGain) * leftBias * softenedDirectGain));
    float rearR = rearLpR_.Process(
        rearHpR_.Process(decorR * (bedGain + spreadGain) * rightBias * softenedDirectGain));
    rearL *= params_.rearLeftTrim;
    rearR *= params_.rearRightTrim;

    rearScratchL_[i] = rearL;
    rearScratchR_[i] = rearR;
    rearEnergy += static_cast<double>(rearL) * rearL +
                  static_cast<double>(rearR) * rearR;
  }

  // Hard rear-energy budget. The average RMS of the rear pair may never exceed rearBudget times
  // the average RMS of the original front pair, regardless of what the detector or tuning asks.
  const double frontRms = std::sqrt(
      (full.eL + full.eR) / (2.0 * static_cast<double>(frames) + kEps));
  const double rearRms = std::sqrt(
      rearEnergy / (2.0 * static_cast<double>(frames) + kEps));
  float budgetScale = 1.0f;
  if (rearRms > kEps && frontRms > kEps)
  {
    const double allowed = static_cast<double>(params_.rearBudget) * frontRms;
    budgetScale = static_cast<float>(std::min(1.0, allowed / rearRms));
  }

  for (size_t i = 0; i < frames; ++i)
  {
    const float l = stereo[2 * i];
    const float r = stereo[2 * i + 1];
    const float midSample = 0.5f * (l + r);

    const float centerBand = centerLp_.Process(centerHp_.Process(midSample));
    const float center =
        params_.centerTrebleGain * centerConfidence * centerBand;

    const float rearL = rearScratchL_[i] * budgetScale;
    const float rearR = rearScratchR_[i] * budgetScale;

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
