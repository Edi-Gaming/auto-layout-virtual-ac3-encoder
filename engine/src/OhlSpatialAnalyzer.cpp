#include "OhlSpatialAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1.0e-20;

bool IsPowerOfTwo(int x)
{
  return x >= 2 && (x & (x - 1)) == 0;
}

void Fft(std::vector<std::complex<double>>& a)
{
  const size_t n = a.size();

  for (size_t i = 1, j = 0; i < n; ++i)
  {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(a[i], a[j]);
  }

  for (size_t len = 2; len <= n; len <<= 1)
  {
    const double angle = -2.0 * kPi / static_cast<double>(len);
    const std::complex<double> wlen(std::cos(angle), std::sin(angle));
    for (size_t i = 0; i < n; i += len)
    {
      std::complex<double> w(1.0, 0.0);
      for (size_t j = 0; j < len / 2; ++j)
      {
        const auto u = a[i + j];
        const auto v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}

double AnalysisFrequencyWeight(double hz)
{
  if (hz < 80.0 || hz > 18000.0)
    return 0.0;
  if (hz < 180.0)
    return 0.15;
  if (hz < 500.0)
    return 0.40;
  if (hz < 5000.0)
    return 0.72;
  if (hz < 14000.0)
    return 1.00;
  return 0.72;
}

double CenterFrequencyWeight(double hz)
{
  if (hz < 120.0 || hz > 10000.0)
    return 0.20;
  if (hz < 250.0)
    return 0.45;
  if (hz < 5000.0)
    return 1.00;
  return 0.55;
}

} // namespace

bool OhlSpatialAnalyzer::Init(const Params& params)
{
  if (params.sampleRate <= 0 ||
      !IsPowerOfTwo(params.fftSize) ||
      params.fftSize < 128 ||
      params.fftSize > 4096 ||
      params.hopSize <= 0 ||
      params.hopSize > params.fftSize ||
      !std::isfinite(params.spatialBinThreshold) ||
      params.spatialBinThreshold < 0.0f ||
      params.spatialBinThreshold > 1.0f)
    return false;

  params_ = params;
  window_.resize(static_cast<size_t>(params_.fftSize));
  for (int i = 0; i < params_.fftSize; ++i)
  {
    window_[static_cast<size_t>(i)] = static_cast<float>(
        0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                             static_cast<double>(params_.fftSize - 1)));
  }

  const size_t bins = static_cast<size_t>(params_.fftSize / 2 + 1);
  prevMagL_.assign(bins, 0.0f);
  prevMagR_.assign(bins, 0.0f);
  havePrevious_ = false;
  return true;
}

void OhlSpatialAnalyzer::Reset()
{
  std::fill(prevMagL_.begin(), prevMagL_.end(), 0.0f);
  std::fill(prevMagR_.begin(), prevMagR_.end(), 0.0f);
  havePrevious_ = false;
}

OhlSpatialAnalyzer::Metrics OhlSpatialAnalyzer::Analyze(const float* stereo, size_t frames)
{
  Metrics out;
  if (!stereo || frames == 0 || window_.empty())
    return out;

  const size_t n = static_cast<size_t>(params_.fftSize);
  const size_t bins = n / 2 + 1;

  struct BinAccum
  {
    double pL = 0.0;
    double pR = 0.0;
    double pM = 0.0;
    double pS = 0.0;
    std::complex<double> cross{0.0, 0.0};
    double flux = 0.0;
    int observations = 0;
  };

  std::vector<BinAccum> accum(bins);
  std::vector<std::complex<double>> left(n);
  std::vector<std::complex<double>> right(n);
  std::vector<float> lastMagL = prevMagL_;
  std::vector<float> lastMagR = prevMagR_;
  bool haveLast = havePrevious_;

  size_t starts = 0;
  if (frames <= n)
  {
    starts = 1;
  }
  else
  {
    starts = 1 + (frames - n) / static_cast<size_t>(params_.hopSize);
    if ((starts - 1) * static_cast<size_t>(params_.hopSize) + n < frames)
      ++starts;
  }

  for (size_t frameIndex = 0; frameIndex < starts; ++frameIndex)
  {
    const size_t start = frameIndex * static_cast<size_t>(params_.hopSize);
    for (size_t i = 0; i < n; ++i)
    {
      const size_t src = start + i;
      const double w = static_cast<double>(window_[i]);
      const double l = src < frames ? static_cast<double>(stereo[2 * src]) : 0.0;
      const double r = src < frames ? static_cast<double>(stereo[2 * src + 1]) : 0.0;
      left[i] = std::complex<double>(l * w, 0.0);
      right[i] = std::complex<double>(r * w, 0.0);
    }

    Fft(left);
    Fft(right);

    for (size_t k = 1; k < bins; ++k)
    {
      const auto l = left[k];
      const auto r = right[k];
      const auto m = 0.5 * (l + r);
      const auto s = 0.5 * (l - r);

      const double pL = std::norm(l);
      const double pR = std::norm(r);
      const double pM = std::norm(m);
      const double pS = std::norm(s);
      const float magL = static_cast<float>(std::sqrt(pL));
      const float magR = static_cast<float>(std::sqrt(pR));

      auto& a = accum[k];
      a.pL += pL;
      a.pR += pR;
      a.pM += pM;
      a.pS += pS;
      a.cross += l * std::conj(r);
      if (haveLast)
      {
        const double dL = std::max(0.0, static_cast<double>(magL - lastMagL[k]));
        const double dR = std::max(0.0, static_cast<double>(magR - lastMagR[k]));
        a.flux += dL * dL + dR * dR;
      }
      ++a.observations;

      lastMagL[k] = magL;
      lastMagR[k] = magR;
    }
    haveLast = true;
  }

  prevMagL_ = std::move(lastMagL);
  prevMagR_ = std::move(lastMagR);
  havePrevious_ = haveLast;

  double ambienceWeighted = 0.0;
  double ambienceWeight = 0.0;
  double activeSpatialWeight = 0.0;
  double totalSpatialEligibleWeight = 0.0;
  double centerWeighted = 0.0;
  double centerWeight = 0.0;
  double hardPanWeighted = 0.0;
  double hardPanWeight = 0.0;
  double coherenceWeighted = 0.0;
  double coherenceWeight = 0.0;
  double transientFlux = 0.0;
  double transientEnergy = 0.0;
  double spatialLeft = 0.0;
  double spatialRight = 0.0;

  for (size_t k = 1; k < bins; ++k)
  {
    const auto& a = accum[k];
    if (a.observations == 0)
      continue;

    const double hz =
        static_cast<double>(k) * static_cast<double>(params_.sampleRate) /
        static_cast<double>(params_.fftSize);
    const double freqWeight = AnalysisFrequencyWeight(hz);
    if (freqWeight <= 0.0)
      continue;

    const double rmsL = std::sqrt(a.pL);
    const double rmsR = std::sqrt(a.pR);
    const double lrSum = rmsL + rmsR;
    if (lrSum <= 1.0e-9)
      continue;

    const double balance =
        std::clamp(2.0 * std::min(rmsL, rmsR) / (lrSum + kEps), 0.0, 1.0);
    const double denom = std::sqrt(a.pL * a.pR);
    const double coherence =
        denom > kEps ? std::clamp(std::abs(a.cross) / denom, 0.0, 1.0) : 1.0;
    const double phaseCos =
        denom > kEps ? std::clamp(a.cross.real() / denom, -1.0, 1.0) : 1.0;
    const double sideFraction =
        std::clamp(a.pS / (a.pM + a.pS + kEps), 0.0, 1.0);

    const double diffuse =
        std::clamp(0.62 * (1.0 - coherence) +
                   0.38 * (1.0 - std::max(phaseCos, 0.0)), 0.0, 1.0);
    const double spatialShape = std::sqrt(sideFraction);
    const double panProtection = 0.08 + 0.92 * balance;

    // Reverb and stereo ambience can be coherent over short windows, so retain a modest
    // side-information term even before full decorrelation. Direct/hard-panned bins are strongly
    // penalized by panProtection.
    const double spatialScore = std::clamp(
        spatialShape * panProtection * (0.22 + 0.78 * diffuse), 0.0, 1.0);

    const double energy = a.pL + a.pR;
    // sqrt-energy weighting prevents a loud centered lead from completely hiding quieter spatial
    // bins while still ignoring numerical FFT dust.
    const double w = freqWeight * std::sqrt(std::sqrt(energy) + 1.0e-18);

    ambienceWeighted += w * spatialScore;
    ambienceWeight += w;
    totalSpatialEligibleWeight += w;
    if (spatialScore >= static_cast<double>(params_.spatialBinThreshold))
    {
      activeSpatialWeight += w;
      const double leftShare = a.pL / (energy + kEps);
      spatialLeft += w * spatialScore * leftShare;
      spatialRight += w * spatialScore * (1.0 - leftShare);
    }

    const double positivePhase =
        std::clamp((phaseCos - 0.35) / 0.65, 0.0, 1.0);
    const double centerScore =
        std::clamp(balance * coherence * positivePhase, 0.0, 1.0);
    const double cw = CenterFrequencyWeight(hz) * std::sqrt(std::sqrt(energy) + 1.0e-18);
    centerWeighted += cw * centerScore;
    centerWeight += cw;

    const double hardPanScore =
        std::clamp((1.0 - balance) * (0.35 + 0.65 * coherence), 0.0, 1.0);
    hardPanWeighted += w * hardPanScore;
    hardPanWeight += w;

    coherenceWeighted += w * coherence;
    coherenceWeight += w;

    transientFlux += freqWeight * a.flux;
    transientEnergy += freqWeight * energy;
  }

  const double weightedAmbience =
      ambienceWeight > kEps ? ambienceWeighted / ambienceWeight : 0.0;
  const double activeFraction =
      totalSpatialEligibleWeight > kEps
          ? activeSpatialWeight / totalSpatialEligibleWeight
          : 0.0;

  // Hybrid score: average confidence says how spatial the eligible spectrum is; active fraction
  // ensures a smaller set of genuinely spatial bins can still open the room instead of being
  // drowned by a louder centered vocal/instrument.
  out.ambience = static_cast<float>(std::clamp(
      0.62 * weightedAmbience + 0.38 * std::sqrt(activeFraction), 0.0, 1.0));
  out.spatialBinFraction =
      static_cast<float>(std::clamp(activeFraction, 0.0, 1.0));
  out.center = static_cast<float>(
      std::clamp(centerWeight > kEps ? centerWeighted / centerWeight : 0.0, 0.0, 1.0));
  out.hardPan = static_cast<float>(
      std::clamp(hardPanWeight > kEps ? hardPanWeighted / hardPanWeight : 0.0, 0.0, 1.0));
  out.meanCoherence = static_cast<float>(
      std::clamp(coherenceWeight > kEps ? coherenceWeighted / coherenceWeight : 1.0, 0.0, 1.0));

  const double fluxNorm =
      transientEnergy > kEps ? transientFlux / transientEnergy : 0.0;
  out.transient = static_cast<float>(
      std::clamp(2.5 * std::sqrt(std::max(0.0, fluxNorm)), 0.0, 1.0));

  const double spatialSum = spatialLeft + spatialRight;
  if (spatialSum > kEps)
  {
    out.spatialLeftShare = static_cast<float>(
        std::clamp(spatialLeft / spatialSum, 0.0, 1.0));
    out.spatialRightShare = 1.0f - out.spatialLeftShare;
  }

  return out;
}
