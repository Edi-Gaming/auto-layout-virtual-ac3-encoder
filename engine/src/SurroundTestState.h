#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

enum class SurroundTestRoute : int
{
  Off = 0,
  Sparkle,
  FL,
  FR,
  C,
  LFE,
  SL,
  SR,
  FL_LFE,
  FL_LFE_M10,
  FL_LFE_INV
};

inline const char* SurroundTestRouteName(SurroundTestRoute route)
{
  switch (route)
  {
    case SurroundTestRoute::Sparkle: return "sparkle";
    case SurroundTestRoute::FL:     return "fl";
    case SurroundTestRoute::FR:     return "fr";
    case SurroundTestRoute::C:      return "c";
    case SurroundTestRoute::LFE:    return "lfe";
    case SurroundTestRoute::SL:     return "sl";
    case SurroundTestRoute::SR:     return "sr";
    case SurroundTestRoute::FL_LFE:     return "fl+lfe";
    case SurroundTestRoute::FL_LFE_M10: return "fl+lfe-10";
    case SurroundTestRoute::FL_LFE_INV: return "fl+lfe-inv";
    case SurroundTestRoute::Off:        return "off";
  }
  return "off";
}

inline bool ParseSurroundTestRoute(const std::string& text, SurroundTestRoute& route)
{
  if (text == "sparkle") route = SurroundTestRoute::Sparkle;
  else if (text == "fl") route = SurroundTestRoute::FL;
  else if (text == "fr") route = SurroundTestRoute::FR;
  else if (text == "c" || text == "center") route = SurroundTestRoute::C;
  else if (text == "lfe" || text == "sub") route = SurroundTestRoute::LFE;
  else if (text == "sl") route = SurroundTestRoute::SL;
  else if (text == "sr") route = SurroundTestRoute::SR;
  else if (text == "fl+lfe" || text == "front+lfe") route = SurroundTestRoute::FL_LFE;
  else if (text == "fl+lfe-10" || text == "front+lfe-10") route = SurroundTestRoute::FL_LFE_M10;
  else if (text == "fl+lfe-inv" || text == "front+lfe-inv") route = SurroundTestRoute::FL_LFE_INV;
  else return false;
  return true;
}

// Cross-thread request state for the native Surround Wizard. The pipe/UI thread only writes
// atomics; the WASAPI render thread owns oscillator/crossfade state and consumes these requests.
struct SurroundTestState
{
  bool Start(SurroundTestRoute route, int frequencyHz, double levelDb)
  {
    if (route == SurroundTestRoute::Off || route == SurroundTestRoute::Sparkle ||
        frequencyHz < 20 || frequencyHz > 20000 ||
        !std::isfinite(levelDb) || levelDb < -60.0 || levelDb > -6.0)
      return false;

    frequencyHz_.store(frequencyHz, std::memory_order_relaxed);
    levelTenthsDb_.store(static_cast<int>(std::lround(levelDb * 10.0)),
                         std::memory_order_relaxed);
    route_.store(static_cast<int>(route), std::memory_order_release);
    revision_.fetch_add(1, std::memory_order_acq_rel);
    return true;
  }

  bool StartSparkle(const std::string& texture, int rateHz, int decayMs, double levelDb)
  {
    int t = -1;
    if (texture == "mixed") t = 0;
    else if (texture == "sine") t = 1;
    else if (texture == "triangle") t = 2;
    else if (texture == "metal") t = 3;
    else if (texture == "noise") t = 4;
    if (t < 0 || rateHz < 1 || rateHz > 24 || decayMs < 60 || decayMs > 1000 ||
        !std::isfinite(levelDb) || levelDb < -60.0 || levelDb > -18.0)
      return false;
    sparkleTexture_.store(t, std::memory_order_relaxed);
    sparkleRate_.store(rateHz, std::memory_order_relaxed);
    sparkleDecayMs_.store(decayMs, std::memory_order_relaxed);
    levelTenthsDb_.store(static_cast<int>(std::lround(levelDb * 10.0)),
                         std::memory_order_relaxed);
    route_.store(static_cast<int>(SurroundTestRoute::Sparkle), std::memory_order_release);
    revision_.fetch_add(1, std::memory_order_acq_rel);
    return true;
  }

  int SparkleTexture() const { return sparkleTexture_.load(std::memory_order_relaxed); }
  int SparkleRateHz() const { return sparkleRate_.load(std::memory_order_relaxed); }
  int SparkleDecayMs() const { return sparkleDecayMs_.load(std::memory_order_relaxed); }

  void Stop()
  {
    route_.store(static_cast<int>(SurroundTestRoute::Off), std::memory_order_release);
    revision_.fetch_add(1, std::memory_order_acq_rel);
  }

  SurroundTestRoute Route() const
  {
    return static_cast<SurroundTestRoute>(route_.load(std::memory_order_acquire));
  }

  int FrequencyHz() const
  {
    return frequencyHz_.load(std::memory_order_relaxed);
  }

  double LevelDb() const
  {
    return static_cast<double>(levelTenthsDb_.load(std::memory_order_relaxed)) / 10.0;
  }

  uint64_t Revision() const
  {
    return revision_.load(std::memory_order_acquire);
  }

  bool Active() const
  {
    return Route() != SurroundTestRoute::Off;
  }

  std::string ToCompactString() const
  {
    const SurroundTestRoute route = Route();
    if (route == SurroundTestRoute::Off)
      return "test_state=idle";

    char buf[192] = {};
    if (route == SurroundTestRoute::Sparkle)
    {
      std::snprintf(buf, sizeof buf,
                    "test_state=active;route=sparkle;texture=%d;rate=%d;decay_ms=%d;level_db=%.1f",
                    SparkleTexture(), SparkleRateHz(), SparkleDecayMs(), LevelDb());
      return buf;
    }
    std::snprintf(buf, sizeof buf,
                  "test_state=active;route=%s;frequency_hz=%d;level_db=%.1f",
                  SurroundTestRouteName(route), FrequencyHz(), LevelDb());
    return buf;
  }

private:
  std::atomic<int> route_{static_cast<int>(SurroundTestRoute::Off)};
  std::atomic<int> sparkleTexture_{0};
  std::atomic<int> sparkleRate_{9};
  std::atomic<int> sparkleDecayMs_{240};
  std::atomic<int> frequencyHz_{80};
  std::atomic<int> levelTenthsDb_{-300};
  std::atomic<uint64_t> revision_{1};
};
