#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

enum class SurroundTestRoute : int
{
  Off = 0,
  FL,
  FR,
  C,
  LFE,
  SL,
  SR,
  FL_LFE
};

inline const char* SurroundTestRouteName(SurroundTestRoute route)
{
  switch (route)
  {
    case SurroundTestRoute::FL:     return "fl";
    case SurroundTestRoute::FR:     return "fr";
    case SurroundTestRoute::C:      return "c";
    case SurroundTestRoute::LFE:    return "lfe";
    case SurroundTestRoute::SL:     return "sl";
    case SurroundTestRoute::SR:     return "sr";
    case SurroundTestRoute::FL_LFE: return "fl+lfe";
    case SurroundTestRoute::Off:    return "off";
  }
  return "off";
}

inline bool ParseSurroundTestRoute(const std::string& text, SurroundTestRoute& route)
{
  if (text == "fl") route = SurroundTestRoute::FL;
  else if (text == "fr") route = SurroundTestRoute::FR;
  else if (text == "c" || text == "center") route = SurroundTestRoute::C;
  else if (text == "lfe" || text == "sub") route = SurroundTestRoute::LFE;
  else if (text == "sl") route = SurroundTestRoute::SL;
  else if (text == "sr") route = SurroundTestRoute::SR;
  else if (text == "fl+lfe" || text == "front+lfe") route = SurroundTestRoute::FL_LFE;
  else return false;
  return true;
}

// Cross-thread request state for the native Surround Wizard. The pipe/UI thread only writes
// atomics; the WASAPI render thread owns oscillator/crossfade state and consumes these requests.
struct SurroundTestState
{
  bool Start(SurroundTestRoute route, int frequencyHz, double levelDb)
  {
    if (route == SurroundTestRoute::Off ||
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

    char buf[160] = {};
    std::snprintf(buf, sizeof buf,
                  "test_state=active;route=%s;frequency_hz=%d;level_db=%.1f",
                  SurroundTestRouteName(route), FrequencyHz(), LevelDb());
    return buf;
  }

private:
  std::atomic<int> route_{static_cast<int>(SurroundTestRoute::Off)};
  std::atomic<int> frequencyHz_{80};
  std::atomic<int> levelTenthsDb_{-300};
  std::atomic<uint64_t> revision_{1};
};
