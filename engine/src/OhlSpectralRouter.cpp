#include "OhlSpectralRouter.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1.0e-20;

bool IsPowerOfTwo(int n)
{
  return n >= 2 && (n & (n - 1)) == 0;
}

float TimeAlpha(float ms, int hop, int sampleRate)
{
  if (ms <= 0.0f || hop <= 0 || sampleRate <= 0)
    return 1.0f;
  const double dt = static_cast<double>(hop) / static_cast<double>(sampleRate);
  return static_cast<float>(1.0 - std::exp(-dt / (static_cast<double>(ms) / 1000.0)));
}

} // namespace

bool OhlSpectralRouter::Init(const Params& params)
{
  if (params.sampleRate <= 0 ||
      !IsPowerOfTwo(params.fftSize) ||
      params.fftSize < 128 ||
      params.fftSize > 2048 ||
      params.hopSize * 2 != params.fftSize ||
      !std::isfinite(params.spatialThreshold) ||
      params.spatialThreshold < 0.0f ||
      params.spatialThreshold > 1.0f ||
      params.acquireMs <= 0.0f ||
      params.releaseMs <= 0.0f ||
      params.surroundGain < 0.0f ||
      params.widthFloor < 0.0f ||
      params.dimension < -1.0f ||
      params.dimension > 1.0f ||
      params.globalFrontLock < 0.0f ||
      params.globalFrontLock > 1.0f)
    return false;

  params_ = params;

  const size_t n = static_cast<size_t>(params_.fftSize);
  const size_t bins = n / 2 + 1;

  window_.resize(n);
  for (size_t i = 0; i < n; ++i)
  {
    // Periodic Hann. sqrt analysis + sqrt synthesis gives exact 50%-overlap COLA.
    const double hann = 0.5 - 0.5 * std::cos(
        2.0 * kPi * static_cast<double>(i) / static_cast<double>(n));
    window_[i] = static_cast<float>(std::sqrt(std::max(0.0, hann)));
  }

  inL_.assign(static_cast<size_t>(params_.hopSize), 0.0f);
  inR_.assign(static_cast<size_t>(params_.hopSize), 0.0f);
  olaL_.assign(n, 0.0f);
  olaR_.assign(n, 0.0f);
  outQueueL_.clear();
  outQueueR_.clear();
  outRead_ = 0;
  initialSilence_ = params_.fftSize;
  discardPrerollHop_ = true;

  smoothPL_.assign(bins, 0.0);
  smoothPR_.assign(bins, 0.0);
  smoothCross_.assign(bins, {0.0, 0.0});
  prevMagL_.assign(bins, 0.0f);
  prevMagR_.assign(bins, 0.0f);
  ownership_.assign(bins, 0.0f);
  metrics_ = {};
  return true;
}

void OhlSpectralRouter::Reset()
{
  std::fill(inL_.begin(), inL_.end(), 0.0f);
  std::fill(inR_.begin(), inR_.end(), 0.0f);
  std::fill(olaL_.begin(), olaL_.end(), 0.0f);
  std::fill(olaR_.begin(), olaR_.end(), 0.0f);
  outQueueL_.clear();
  outQueueR_.clear();
  outRead_ = 0;
  initialSilence_ = params_.fftSize;
  discardPrerollHop_ = true;

  std::fill(smoothPL_.begin(), smoothPL_.end(), 0.0);
  std::fill(smoothPR_.begin(), smoothPR_.end(), 0.0);
  std::fill(smoothCross_.begin(), smoothCross_.end(), std::complex<double>(0.0, 0.0));
  std::fill(prevMagL_.begin(), prevMagL_.end(), 0.0f);
  std::fill(prevMagR_.begin(), prevMagR_.end(), 0.0f);
  std::fill(ownership_.begin(), ownership_.end(), 0.0f);
  metrics_ = {};
}

int OhlSpectralRouter::BandForHz(double hz)
{
  if (hz < 250.0) return 0;
  if (hz < 2000.0) return 1;
  if (hz < 6000.0) return 2;
  return 3;
}

void OhlSpectralRouter::Fft(std::vector<std::complex<double>>& a, bool inverse)
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

  const double sign = inverse ? 1.0 : -1.0;
  for (size_t len = 2; len <= n; len <<= 1)
  {
    const double angle = sign * 2.0 * kPi / static_cast<double>(len);
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

  if (inverse)
  {
    const double scale = 1.0 / static_cast<double>(n);
    for (auto& v : a)
      v *= scale;
  }
}

std::complex<double> OhlSpectralRouter::AllpassResponse(double omega, double a) const
{
  const std::complex<double> z1(std::cos(omega), -std::sin(omega));
  return (z1 - a) / (1.0 - a * z1);
}

void OhlSpectralRouter::ProcessFrame()
{
  const size_t n = static_cast<size_t>(params_.fftSize);
  const size_t h = static_cast<size_t>(params_.hopSize);
  const size_t bins = n / 2 + 1;

  std::vector<std::complex<double>> l(n);
  std::vector<std::complex<double>> r(n);
  for (size_t i = 0; i < n; ++i)
  {
    l[i] = std::complex<double>(static_cast<double>(inL_[i] * window_[i]), 0.0);
    r[i] = std::complex<double>(static_cast<double>(inR_[i] * window_[i]), 0.0);
  }
  Fft(l, false);
  Fft(r, false);

  std::vector<std::complex<double>> outL(n, {0.0, 0.0});
  std::vector<std::complex<double>> outR(n, {0.0, 0.0});

  const float acquireAlpha = TimeAlpha(params_.acquireMs, params_.hopSize, params_.sampleRate);
  const float releaseAlpha = TimeAlpha(params_.releaseMs, params_.hopSize, params_.sampleRate);
  const double dimensionGain = std::pow(2.0, 0.85 * static_cast<double>(params_.dimension));

  std::array<double, kBands> ownershipSum{{0,0,0,0}};
  std::array<double, kBands> centerSum{{0,0,0,0}};
  std::array<double, kBands> bandWeight{{0,0,0,0}};
  double active = 0.0;
  double eligible = 0.0;
  double transientWeighted = 0.0;
  double transientWeight = 0.0;

  for (size_t k = 1; k < bins; ++k)
  {
    const double hz =
        static_cast<double>(k) * static_cast<double>(params_.sampleRate) /
        static_cast<double>(params_.fftSize);
    if (hz < 70.0 || hz > 19000.0)
      continue;

    const double pL = std::norm(l[k]);
    const double pR = std::norm(r[k]);
    const double energy = pL + pR;
    if (energy <= 1.0e-14)
    {
      // Silence is evidence that this bin is no longer spatial. Do not freeze ownership across
      // gaps; release it with the same hysteresis time constant used for weak/non-spatial content.
      ownership_[k] += releaseAlpha * (0.0f - ownership_[k]);
      if (ownership_[k] < 1.0e-5f) ownership_[k] = 0.0f;
      smoothPL_[k] *= 0.80;
      smoothPR_[k] *= 0.80;
      smoothCross_[k] *= 0.80;
      prevMagL_[k] = 0.0f;
      prevMagR_[k] = 0.0f;
      continue;
    }

    const double magL = std::sqrt(pL);
    const double magR = std::sqrt(pR);
    const double balance =
        std::clamp(2.0 * std::min(magL, magR) / (magL + magR + kEps), 0.0, 1.0);

    // Temporal coherence estimate. This is the persistence memory that a single FFT frame cannot
    // provide on its own.
    constexpr double smooth = 0.80;
    smoothPL_[k] = smooth * smoothPL_[k] + (1.0 - smooth) * pL;
    smoothPR_[k] = smooth * smoothPR_[k] + (1.0 - smooth) * pR;
    smoothCross_[k] =
        smooth * smoothCross_[k] + (1.0 - smooth) * (l[k] * std::conj(r[k]));

    const double denom = std::sqrt(smoothPL_[k] * smoothPR_[k]);
    const double coherence =
        denom > kEps ? std::clamp(std::abs(smoothCross_[k]) / denom, 0.0, 1.0) : 1.0;
    const double phaseCos =
        denom > kEps ? std::clamp(smoothCross_[k].real() / denom, -1.0, 1.0) : 1.0;

    const auto mid = 0.5 * (l[k] + r[k]);
    const auto side = 0.5 * (l[k] - r[k]);
    const double sideFraction =
        std::clamp(std::norm(side) / (std::norm(mid) + std::norm(side) + kEps), 0.0, 1.0);

    const double diffuse =
        std::clamp(0.58 * (1.0 - coherence) +
                   0.42 * (1.0 - std::max(phaseCos, 0.0)), 0.0, 1.0);
    const double spatialScore = std::clamp(
        std::sqrt(sideFraction) *
        (0.06 + 0.94 * balance) *
        (0.18 + 0.82 * diffuse),
        0.0, 1.0);

    const double positivePhase =
        std::clamp((phaseCos - 0.30) / 0.70, 0.0, 1.0);
    const double centerScore =
        std::clamp(balance * coherence * positivePhase, 0.0, 1.0);

    const double hardPan =
        std::clamp((1.0 - balance) * (0.35 + 0.65 * coherence), 0.0, 1.0);

    const float magLf = static_cast<float>(magL);
    const float magRf = static_cast<float>(magR);
    const double flux =
        std::max(0.0, static_cast<double>(magLf - prevMagL_[k])) +
        std::max(0.0, static_cast<double>(magRf - prevMagR_[k]));
    prevMagL_[k] = magLf;
    prevMagR_[k] = magRf;
    const double transient = std::clamp(flux / (magL + magR + 1.0e-9), 0.0, 1.0);

    // Hysteresis: once a bin has spatial ownership, it is allowed to hold until confidence falls
    // substantially below the acquire threshold. Ownership itself then follows separate attack
    // and release time constants.
    const double on = static_cast<double>(params_.spatialThreshold);
    const double off = 0.62 * on;
    float target = ownership_[k];
    if (spatialScore >= on)
      target = 1.0f;
    else if (spatialScore <= off)
      target = 0.0f;

    const float alpha = target > ownership_[k] ? acquireAlpha : releaseAlpha;
    ownership_[k] += alpha * (target - ownership_[k]);
    ownership_[k] = std::clamp(ownership_[k], 0.0f, 1.0f);

    const int band = BandForHz(hz);
    const double steering = std::max(0.0f, params_.steering[static_cast<size_t>(band)]);
    const double bandFrontLock =
        std::clamp(static_cast<double>(params_.globalFrontLock) *
                   static_cast<double>(params_.frontLock[static_cast<size_t>(band)]) *
                   centerScore, 0.0, 1.0);
    const double frontGain = 1.0 - bandFrontLock;

    // A very small bed remains even before ownership is acquired; the owned component is dominant.
    const double bed = static_cast<double>(params_.widthFloor) *
                       (0.10 + 0.22 * std::sqrt(sideFraction));
    const double owned = static_cast<double>(params_.surroundGain) *
                         static_cast<double>(ownership_[k]);
    const double directGain =
        1.0 - 0.55 * static_cast<double>(params_.directReject) *
                    std::max(hardPan, transient);
    const double gain = std::max(
        0.0,
        (bed + owned) * steering * frontGain * directGain * dimensionGain);

    const double leftShare = pL / (energy + kEps);
    const double rightShare = 1.0 - leftShare;
    const double leftBias = 0.12 + 0.88 * std::clamp(2.0 * leftShare, 0.0, 1.0);
    const double rightBias = 0.12 + 0.88 * std::clamp(2.0 * rightShare, 0.0, 1.0);

    const double omega = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(params_.fftSize);
    const auto apL = AllpassResponse(omega, 0.43) * AllpassResponse(omega, -0.61);
    const auto apR = AllpassResponse(omega, -0.37) * AllpassResponse(omega, 0.69);

    outL[k] = side * (gain * leftBias) * apL;
    outR[k] = -side * (gain * rightBias) * apR;

    // Hermitian symmetry for real IFFT.
    if (k != n - k)
    {
      outL[n - k] = std::conj(outL[k]);
      outR[n - k] = std::conj(outR[k]);
    }

    const double w = std::sqrt(energy + 1.0e-18);
    ownershipSum[static_cast<size_t>(band)] += w * ownership_[k];
    centerSum[static_cast<size_t>(band)] += w * centerScore;
    bandWeight[static_cast<size_t>(band)] += w;
    eligible += w;
    active += w * ownership_[k];
    transientWeighted += w * transient;
    transientWeight += w;
  }

  Fft(outL, true);
  Fft(outR, true);

  for (size_t i = 0; i < n; ++i)
  {
    olaL_[i] += static_cast<float>(outL[i].real()) * window_[i];
    olaR_[i] += static_cast<float>(outR[i].real()) * window_[i];
  }

  if (discardPrerollHop_)
  {
    // The first frame spans the synthetic -hop..+hop preroll. Its first overlap-add half belongs
    // before time zero and must not appear in the causal output stream.
    discardPrerollHop_ = false;
  }
  else
  {
    for (size_t i = 0; i < h; ++i)
    {
      outQueueL_.push_back(olaL_[i]);
      outQueueR_.push_back(olaR_[i]);
    }
  }

  std::move(olaL_.begin() + static_cast<std::ptrdiff_t>(h), olaL_.end(), olaL_.begin());
  std::move(olaR_.begin() + static_cast<std::ptrdiff_t>(h), olaR_.end(), olaR_.begin());
  std::fill(olaL_.end() - static_cast<std::ptrdiff_t>(h), olaL_.end(), 0.0f);
  std::fill(olaR_.end() - static_cast<std::ptrdiff_t>(h), olaR_.end(), 0.0f);

  for (int b = 0; b < kBands; ++b)
  {
    if (bandWeight[static_cast<size_t>(b)] > kEps)
    {
      metrics_.ownership[static_cast<size_t>(b)] = static_cast<float>(
          ownershipSum[static_cast<size_t>(b)] / bandWeight[static_cast<size_t>(b)]);
      metrics_.center[static_cast<size_t>(b)] = static_cast<float>(
          centerSum[static_cast<size_t>(b)] / bandWeight[static_cast<size_t>(b)]);
    }
    else
    {
      metrics_.ownership[static_cast<size_t>(b)] = 0.0f;
      metrics_.center[static_cast<size_t>(b)] = 0.0f;
    }
  }
  metrics_.meanOwnership =
      static_cast<float>(eligible > kEps ? active / eligible : 0.0);
  metrics_.activeFraction = metrics_.meanOwnership;
  metrics_.transient =
      static_cast<float>(transientWeight > kEps ? transientWeighted / transientWeight : 0.0);

  // Advance input by one hop after the processed frame.
  inL_.erase(inL_.begin(), inL_.begin() + static_cast<std::ptrdiff_t>(h));
  inR_.erase(inR_.begin(), inR_.begin() + static_cast<std::ptrdiff_t>(h));
}

void OhlSpectralRouter::Process(const float* stereo, size_t frames, float* rearL, float* rearR)
{
  if (!stereo || !rearL || !rearR || frames == 0 || window_.empty())
    return;

  for (size_t i = 0; i < frames; ++i)
  {
    inL_.push_back(stereo[2 * i]);
    inR_.push_back(stereo[2 * i + 1]);

    while (inL_.size() >= static_cast<size_t>(params_.fftSize))
      ProcessFrame();

    float yL = 0.0f;
    float yR = 0.0f;
    if (initialSilence_ > 0)
    {
      --initialSilence_;
    }
    else if (outRead_ < outQueueL_.size())
    {
      yL = outQueueL_[outRead_];
      yR = outQueueR_[outRead_];
      ++outRead_;
    }

    rearL[i] = yL;
    rearR[i] = yR;
  }

  if (outRead_ > 4096 && outRead_ * 2 > outQueueL_.size())
  {
    outQueueL_.erase(outQueueL_.begin(), outQueueL_.begin() + static_cast<std::ptrdiff_t>(outRead_));
    outQueueR_.erase(outQueueR_.begin(), outQueueR_.begin() + static_cast<std::ptrdiff_t>(outRead_));
    outRead_ = 0;
  }
}
