#include "MusicSettings.h"
#include "BrandIcon.h"
#include "ModeControl.h"
#include "ModernControls.h"
#include "MacroKnob.h"
#include "AnalyzerVisual.h"
#include "StageVisual.h"

#include <commctrl.h>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kClassName[] = L"OhlMusicSettingsWindow";

enum ControlId
{
  kEnable = 3001,
  kAmbience,
  kWidth,
  kSpectralIntelligence,
  kSpatialBinThreshold,
  kPerBinRouting,
  kDimension,
  kCenterWidth,
  kFrontLock,
  kDiffuseThreshold,
  kRearBudget,
  kReject,
  kCenter,

  kAmbienceValue,
  kWidthValue,
  kSpectralIntelligenceValue,
  kPerBinRoutingValue,
  kDimensionValue,
  kCenterWidthValue,
  kFrontLockValue,
  kRejectValue,
  kCenterValue,

  kLowWeight,
  kMidWeight,
  kHighWeight,
  kLowWeightValue,
  kMidWeightValue,
  kHighWeightValue,

  kAmbienceAttack,
  kAmbienceRelease,
  kSpectralAcquire,
  kSpectralRelease,
  kSteerLow,
  kSteerBody,
  kSteerPresence,
  kSteerAir,
  kLockLow,
  kLockBody,
  kLockPresence,
  kLockAir,
  kDirectThreshold,
  kDirectRecovery,

  kCenterHp,
  kCenterLp,
  kRearHp,
  kRearLp,
  kRearLeftTrim,
  kRearRightTrim,

  kFl,
  kC,
  kFr,
  kSl,
  kSr,

  kPresetNatural,
  kPresetWide,
  kPresetAmbient,
  kPresetV03,
  kViewMix,
  kViewLab,
  kStoreA,
  kRecallA,
  kStoreB,
  kRecallB,
  kAnalyzerViz,
  kStageViz,
  kMeterSummary,
  kMeterBands,
  kSurroundWizard,
  kCapture,
  kCaptureState,
  kReload,
  kApply,
  kStatus
};

HBRUSH gBg = nullptr;
HBRUSH gEditBg = nullptr;
HFONT gTitleFont = nullptr;
HFONT gUiFont = nullptr;
HFONT gSmallFont = nullptr;

constexpr UINT kMetricsMessage = WM_APP + 77;
constexpr UINT kCaptureMessage = WM_APP + 78;
constexpr int kMetricsIntervalMs = 20;
std::atomic_bool gMetricsStop{false};
std::thread gMetricsThread;
OhlAnalyzerMetrics gLastUiMetrics{};
std::vector<HWND> gMixControls;
std::vector<HWND> gLabControls;
std::vector<RECT> gCardRects;
bool gMixPage = true;
bool gHaveSnapshotA = false;
bool gHaveSnapshotB = false;

struct SettingsState
{
  std::string configPath;
  bool enabled = true;

  double ambience = 0.70;
  double width = 0.16;
  double lowWeight = 0.08;
  double midWeight = 0.46;
  double highWeight = 0.46;
  double ambienceAttackMs = 100.0;
  double ambienceReleaseMs = 520.0;
  double spectralIntelligence = 0.90;
  double spatialBinThreshold = 0.30;
  double perBinRouting = 0.55;
  double spectralAcquireMs = 65.0;
  double spectralReleaseMs = 520.0;
  double dimension = 0.0;
  double centerWidth = 1.0;
  double steerLow = 0.18;
  double steerBody = 0.55;
  double steerPresence = 0.90;
  double steerAir = 1.10;
  double lockLow = 0.30;
  double lockBody = 1.00;
  double lockPresence = 0.82;
  double lockAir = 0.25;

  double frontLock = 0.88;
  double diffuseThreshold = 0.10;
  double rearBudget = 0.22;
  double directReject = 0.78;
  double directThreshold = 1.45;
  double directRecoveryMs = 18.0;

  double center = 0.18;
  double centerHp = 2400.0;
  double centerLp = 16000.0;

  double rearHp = 160.0;
  double rearLp = 18000.0;
  double rearLeftTrim = 1.0;
  double rearRightTrim = 1.0;

  double fl = 33.0;
  double c = 30.0;
  double fr = 33.0;
  double sl = 27.0;
  double sr = 33.0;
};

SettingsState gSnapshotA;
SettingsState gSnapshotB;

std::string Trim(const std::string& s)
{
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  const size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::map<std::string, std::string> ReadValues(const std::string& path)
{
  std::map<std::string, std::string> out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line))
  {
    const std::string s = Trim(line);
    if (s.empty() || s[0] == '#' || s[0] == ';') continue;
    const size_t eq = s.find('=');
    if (eq == std::string::npos) continue;
    out[Trim(s.substr(0, eq))] = Trim(s.substr(eq + 1));
  }
  return out;
}

double ReadDouble(const std::map<std::string, std::string>& v,
                  const char* key,
                  double fallback)
{
  auto it = v.find(key);
  if (it == v.end()) return fallback;
  char* end = nullptr;
  const double x = std::strtod(it->second.c_str(), &end);
  return (end && end != it->second.c_str() && std::isfinite(x)) ? x : fallback;
}

void LoadState(SettingsState& s)
{
  const auto v = ReadValues(s.configPath);
  auto it = v.find("stereo_processing");
  s.enabled = it != v.end() && it->second == "music";

  s.ambience = ReadDouble(v, "music_surround_gain", s.ambience);
  s.width = ReadDouble(v, "music_width_floor", s.width);
  s.lowWeight = ReadDouble(v, "music_ambience_low_weight", s.lowWeight);
  s.midWeight = ReadDouble(v, "music_ambience_mid_weight", s.midWeight);
  s.highWeight = ReadDouble(v, "music_ambience_high_weight", s.highWeight);
  s.ambienceAttackMs = ReadDouble(v, "music_ambience_attack_ms", s.ambienceAttackMs);
  s.ambienceReleaseMs = ReadDouble(v, "music_ambience_release_ms", s.ambienceReleaseMs);
  s.spectralIntelligence =
      ReadDouble(v, "music_spectral_intelligence", s.spectralIntelligence);
  s.spatialBinThreshold =
      ReadDouble(v, "music_spatial_bin_threshold", s.spatialBinThreshold);
  s.perBinRouting = ReadDouble(v, "music_per_bin_routing", s.perBinRouting);
  s.spectralAcquireMs = ReadDouble(v, "music_spectral_acquire_ms", s.spectralAcquireMs);
  s.spectralReleaseMs = ReadDouble(v, "music_spectral_release_ms", s.spectralReleaseMs);
  s.dimension = ReadDouble(v, "music_dimension", s.dimension);
  s.centerWidth = ReadDouble(v, "music_center_width", s.centerWidth);
  s.steerLow = ReadDouble(v, "music_steering_low", s.steerLow);
  s.steerBody = ReadDouble(v, "music_steering_body", s.steerBody);
  s.steerPresence = ReadDouble(v, "music_steering_presence", s.steerPresence);
  s.steerAir = ReadDouble(v, "music_steering_air", s.steerAir);
  s.lockLow = ReadDouble(v, "music_front_lock_low", s.lockLow);
  s.lockBody = ReadDouble(v, "music_front_lock_body", s.lockBody);
  s.lockPresence = ReadDouble(v, "music_front_lock_presence", s.lockPresence);
  s.lockAir = ReadDouble(v, "music_front_lock_air", s.lockAir);

  s.frontLock = ReadDouble(v, "music_front_lock", s.frontLock);
  s.diffuseThreshold = ReadDouble(v, "music_diffuse_threshold", s.diffuseThreshold);
  s.rearBudget = ReadDouble(v, "music_rear_budget", s.rearBudget);
  s.directReject = ReadDouble(v, "music_direct_reject", s.directReject);
  s.directThreshold = ReadDouble(v, "music_direct_threshold", s.directThreshold);
  s.directRecoveryMs = ReadDouble(v, "music_direct_recovery_ms", s.directRecoveryMs);

  s.center = ReadDouble(v, "music_center_treble_gain", s.center);
  s.centerHp = ReadDouble(v, "music_center_treble_hz", s.centerHp);
  s.centerLp = ReadDouble(v, "music_center_lowpass_hz", s.centerLp);

  s.rearHp = ReadDouble(v, "music_rear_highpass_hz", s.rearHp);
  s.rearLp = ReadDouble(v, "music_rear_lowpass_hz", s.rearLp);
  s.rearLeftTrim = ReadDouble(v, "music_rear_left_trim", s.rearLeftTrim);
  s.rearRightTrim = ReadDouble(v, "music_rear_right_trim", s.rearRightTrim);

  s.fl = ReadDouble(v, "music_distance_fl_in", s.fl);
  s.c = ReadDouble(v, "music_distance_c_in", s.c);
  s.fr = ReadDouble(v, "music_distance_fr_in", s.fr);
  s.sl = ReadDouble(v, "music_distance_sl_in", s.sl);
  s.sr = ReadDouble(v, "music_distance_sr_in", s.sr);
}

std::string Fmt(double x, int precision = 2)
{
  std::ostringstream o;
  o << std::fixed << std::setprecision(precision) << x;
  return o.str();
}

std::map<std::string, std::string> ParseCompactFields(const std::string& text)
{
  std::map<std::string, std::string> out;
  size_t start = 0;
  while (start < text.size())
  {
    const size_t end = text.find(';', start);
    const std::string token = text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    const size_t eq = token.find('=');
    if (eq != std::string::npos)
      out[token.substr(0, eq)] = token.substr(eq + 1);
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return out;
}

double MetricDouble(const std::map<std::string, std::string>& fields,
                    const char* key,
                    double fallback = 0.0)
{
  auto it = fields.find(key);
  if (it == fields.end()) return fallback;
  char* end = nullptr;
  const double v = std::strtod(it->second.c_str(), &end);
  return (end && end != it->second.c_str() && std::isfinite(v)) ? v : fallback;
}

std::array<double, 4> MetricQuad(const std::map<std::string, std::string>& fields,
                                 const char* key)
{
  std::array<double, 4> out{{0, 0, 0, 0}};
  auto it = fields.find(key);
  if (it == fields.end()) return out;
  std::istringstream in(it->second);
  std::string item;
  for (size_t i = 0; i < out.size() && std::getline(in, item, ','); ++i)
  {
    char* end = nullptr;
    const double v = std::strtod(item.c_str(), &end);
    if (end && end != item.c_str() && std::isfinite(v))
      out[i] = v;
  }
  return out;
}

std::array<double, 6> MetricSix(const std::map<std::string, std::string>& fields,
                                const char* key)
{
  std::array<double, 6> out{{0, 0, 0, 0, 0, 0}};
  auto it = fields.find(key);
  if (it == fields.end()) return out;
  std::istringstream in(it->second);
  std::string item;
  for (size_t i = 0; i < out.size() && std::getline(in, item, ','); ++i)
  {
    char* end = nullptr;
    const double v = std::strtod(item.c_str(), &end);
    if (end && end != item.c_str() && std::isfinite(v))
      out[i] = v;
  }
  return out;
}

std::wstring WidenCaptureUtf8(const std::string& text)
{
  if (text.empty()) return {};
  const int count = MultiByteToWideChar(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) return {};
  std::wstring out(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
  return out;
}

void LaunchSurroundWizard()
{
  wchar_t exe[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring path;
  if (n > 0 && n < MAX_PATH)
  {
    path.assign(exe, n);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
      path = path.substr(0, slash + 1) + L"OHL-Control.exe";
    else
      path = L"OHL-Control.exe";
  }
  else
  {
    path = L"OHL-Control.exe";
  }

  ShellExecuteW(
      nullptr, L"open", path.c_str(), L"--surround-wizard", nullptr, SW_SHOWNORMAL);
}

void UpdateCaptureUi(HWND hwnd, const std::string& response)
{
  const auto fields = ParseCompactFields(response);
  auto stateIt = fields.find("capture_state");
  const std::string state =
      stateIt == fields.end() ? "offline" : stateIt->second;

  const bool active =
      state == "armed" || state == "recording" ||
      state == "complete" || state == "saving";
  SetWindowTextW(
      GetDlgItem(hwnd, kCapture),
      active ? L"CANCEL" : L"CAPTURE 15s");

  std::wstring label;
  if (state == "armed")
  {
    label = L"ARMED — waiting for OHL stereo";
  }
  else if (state == "recording")
  {
    const double elapsed = MetricDouble(fields, "seconds");
    const double requested = MetricDouble(fields, "requested_seconds", 15.0);
    std::wostringstream out;
    out << L"RECORDING "
        << std::fixed << std::setprecision(1)
        << elapsed << L" / " << requested << L" s";
    label = out.str();
  }
  else if (state == "complete" || state == "saving")
  {
    label = L"SAVING CAPTURE...";
  }
  else if (state == "saved")
  {
    std::string path;
    auto pathIt = fields.find("path");
    if (pathIt != fields.end()) path = pathIt->second;
    const size_t slash = path.find_last_of("\\/");
    const std::string leaf =
        slash == std::string::npos ? path : path.substr(slash + 1);
    label = leaf.empty()
        ? L"SAVED"
        : L"SAVED — " + WidenCaptureUtf8(leaf);
  }
  else if (state == "cancelled")
  {
    label = L"CANCELLED";
  }
  else if (state == "error")
  {
    label = L"ERROR";
    auto errorIt = fields.find("error");
    if (errorIt != fields.end() && !errorIt->second.empty())
      label += L" — " + WidenCaptureUtf8(errorIt->second);
  }
  else
  {
    label = L"READY";
  }

  SetWindowTextW(GetDlgItem(hwnd, kCaptureState), label.c_str());
}

OhlAnalyzerMetrics ParseMetricsResponse(const std::string& response)
{
  OhlAnalyzerMetrics m;
  const auto f = ParseCompactFields(response);
  if (f.empty())
    return m;

  m.online = true;
  m.sequence = static_cast<uint64_t>((std::max)(0.0, MetricDouble(f, "seq")));
  m.ambience = static_cast<float>(MetricDouble(f, "amb"));
  m.center = static_cast<float>(MetricDouble(f, "center"));
  m.spatialBins = static_cast<float>(MetricDouble(f, "bins"));
  m.transient = static_cast<float>(MetricDouble(f, "trans"));
  m.rearOpen = static_cast<float>(MetricDouble(f, "rear"));
  m.frontLock = static_cast<float>(MetricDouble(f, "lock"));
  m.budgetScale = static_cast<float>(MetricDouble(f, "budget", 1.0));

  const auto spk = MetricSix(f, "spk");
  for (size_t i = 0; i < m.speakerRms.size(); ++i)
    m.speakerRms[i] = static_cast<float>(spk[i]);

  const auto own = MetricQuad(f, "own");
  const auto ctr = MetricQuad(f, "bandcenter");
  for (size_t i = 0; i < 4; ++i)
  {
    m.ownership[i] = static_cast<float>(own[i]);
    m.bandCenter[i] = static_cast<float>(ctr[i]);
  }
  return m;
}

void StopMetricsWorker(HWND hwnd)
{
  gMetricsStop.store(true);
  if (gMetricsThread.joinable())
    gMetricsThread.join();

  MSG pending{};
  while (PeekMessageW(&pending, hwnd, kMetricsMessage, kMetricsMessage, PM_REMOVE))
    delete reinterpret_cast<OhlAnalyzerMetrics*>(pending.lParam);
  while (PeekMessageW(&pending, hwnd, kCaptureMessage, kCaptureMessage, PM_REMOVE))
    delete reinterpret_cast<std::string*>(pending.lParam);
}

void StartMetricsWorker(HWND hwnd)
{
  StopMetricsWorker(hwnd);
  gMetricsStop.store(false);

  gMetricsThread = std::thread([hwnd]()
  {
    auto next = std::chrono::steady_clock::now();
    OhlAnalyzerMetrics lastGood{};
    uint64_t lastPostedSequence = (std::numeric_limits<uint64_t>::max)();
    int consecutiveMisses = 0;
    bool offlinePosted = false;
    int capturePollTicks = 0;

    while (!gMetricsStop.load())
    {
      std::string response;
      if (SendModeCommand("metrics", response, 40))
      {
        const OhlAnalyzerMetrics parsed = ParseMetricsResponse(response);
        if (parsed.online)
        {
          consecutiveMisses = 0;
          offlinePosted = false;
          lastGood = parsed;

          // The engine updates telemetry once per AC3 music packet. Do not repaint the UI for
          // duplicate named-pipe reads of the same packet.
          if (parsed.sequence != lastPostedSequence)
          {
            auto* metrics = new OhlAnalyzerMetrics(parsed);
            if (!PostMessageW(hwnd, kMetricsMessage, 0, reinterpret_cast<LPARAM>(metrics)))
            {
              delete metrics;
              break;
            }
            lastPostedSequence = parsed.sequence;
          }
        }
      }
      else
      {
        ++consecutiveMisses;

        // A single named-pipe timeout used to flash the analyzer to zero/offline. Hold the last
        // valid frame through short misses and only declare offline after a sustained outage.
        if (consecutiveMisses >= 12 && !offlinePosted)
        {
          auto* metrics = new OhlAnalyzerMetrics(lastGood);
          metrics->online = false;
          if (!PostMessageW(hwnd, kMetricsMessage, 0, reinterpret_cast<LPARAM>(metrics)))
          {
            delete metrics;
            break;
          }
          offlinePosted = true;
        }
      }

      if (++capturePollTicks >= 10)
      {
        capturePollTicks = 0;
        std::string captureResponse;
        if (!SendModeCommand("capture status", captureResponse, 60))
          captureResponse = "capture_state=offline";

        auto* captureStatus = new std::string(captureResponse);
        if (!PostMessageW(
                hwnd,
                kCaptureMessage,
                0,
                reinterpret_cast<LPARAM>(captureStatus)))
        {
          delete captureStatus;
          break;
        }
      }

      next += std::chrono::milliseconds(kMetricsIntervalMs);
      const auto now = std::chrono::steady_clock::now();
      if (next < now)
        next = now;
      std::this_thread::sleep_until(next);
    }
  });
}

bool WriteValues(const std::string& path, const std::map<std::string, std::string>& values)
{
  std::vector<std::string> lines;
  {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
      lines.push_back(line);
  }

  std::map<std::string, bool> seen;
  for (const auto& kv : values) seen[kv.first] = false;

  for (std::string& line : lines)
  {
    const std::string s = Trim(line);
    if (s.empty() || s[0] == '#' || s[0] == ';') continue;
    const size_t eq = s.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(s.substr(0, eq));
    auto it = values.find(key);
    if (it != values.end() && !seen[key])
    {
      line = key + "=" + it->second;
      seen[key] = true;
    }
  }

  bool haveMissing = false;
  for (const auto& kv : values)
    if (!seen[kv.first])
      haveMissing = true;

  if (haveMissing)
  {
    if (!lines.empty() && !lines.back().empty())
      lines.push_back("");
    lines.push_back("# OHL Music settings (managed by the native settings UI)");
    for (const auto& kv : values)
      if (!seen[kv.first])
        lines.push_back(kv.first + "=" + kv.second);
  }

  const std::string tmp = path + ".ohl-tmp";
  {
    std::ofstream f(tmp, std::ios::trunc);
    if (!f) return false;
    for (const auto& line : lines)
      f << line << "\n";
    if (!f) return false;
  }

  if (!MoveFileExA(tmp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
  {
    DeleteFileA(tmp.c_str());
    return false;
  }
  return true;
}

bool PointInsideCard(int x, int y)
{
  POINT p{x, y};
  for (const RECT& r : gCardRects)
    if (PtInRect(&r, p))
      return true;
  return false;
}

HWND Label(HWND parent, const wchar_t* text, int x, int y, int w, int h, bool useSmallFont = false)
{
  const bool onCard = PointInsideCard(x + 2, y + 2);
  HWND c = CreateWindowW(
      onCard ? kOhlCardLabelClass : L"STATIC",
      text,
      WS_CHILD | WS_VISIBLE,
      x, y, w, h,
      parent, nullptr, nullptr, nullptr);
  SendMessageW(c, WM_SETFONT,
               reinterpret_cast<WPARAM>(useSmallFont ? gSmallFont : gUiFont), TRUE);
  return c;
}

HWND Group(HWND parent, const wchar_t* text, int x, int y, int w, int h)
{
  gCardRects.push_back(RECT{x, y, x + w, y + h});
  HWND c = CreateWindowW(kOhlGroupClass, text, WS_CHILD | WS_VISIBLE,
                         x, y, w, h, parent, nullptr, nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  return c;
}

HWND Edit(HWND parent, int id, int x, int y, int w)
{
  HWND c = CreateWindowExW(0, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                           x, y, w, 24, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  return c;
}

HWND Slider(HWND parent, int id, int x, int y, int w, int maxValue, int pos)
{
  HWND c = CreateWindowW(kOhlSliderClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                         x, y, w, 32, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
  SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(0, maxValue));
  SendMessageW(c, TBM_SETPOS, TRUE, pos);
  return c;
}

HWND Knob(HWND parent,
          int id,
          const wchar_t* caption,
          int x,
          int y,
          int w,
          int h,
          int maxValue,
          int pos,
          COLORREF accent)
{
  HWND c = CreateWindowW(
      kOhlMacroKnobClass,
      caption,
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      x, y, w, h, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(0, maxValue));
  SendMessageW(c, TBM_SETPOS, TRUE, pos);
  SendMessageW(c, OHL_KNOB_SET_ACCENT, 0, static_cast<LPARAM>(accent));
  return c;
}

HWND ValueLabel(HWND parent, int id, int x, int y, int w = 62)
{
  HWND c = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                         x, y, w, 22, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  return c;
}

HWND Button(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h = 30)
{
  HWND c = CreateWindowW(kOhlButtonClass, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                         x, y, w, h, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  return c;
}

void ShowControls(const std::vector<HWND>& controls, bool show)
{
  for (HWND control : controls)
    if (control)
      ShowWindow(control, show ? SW_SHOW : SW_HIDE);
}

void SetUiPage(HWND hwnd, bool mix)
{
  gMixPage = mix;
  ShowControls(gMixControls, mix);
  ShowControls(gLabControls, !mix);

  SendDlgItemMessageW(hwnd, kViewMix, BM_SETCHECK, mix ? BST_CHECKED : BST_UNCHECKED, 0);
  SendDlgItemMessageW(hwnd, kViewLab, BM_SETCHECK, mix ? BST_UNCHECKED : BST_CHECKED, 0);

  SetDlgItemTextW(
      hwnd,
      kStatus,
      mix ? L"MIX view \u2014 tune by ear; LAB holds the engineering controls."
          : L"LAB view \u2014 advanced detector, matrix, voicing and geometry tuning.");
  InvalidateRect(hwnd, nullptr, FALSE);
}

void SetDoubleEdit(HWND hwnd, int id, double value, int precision = 1)
{
  const std::string s = Fmt(value, precision);
  SetWindowTextA(GetDlgItem(hwnd, id), s.c_str());
}

double GetDoubleEdit(HWND hwnd, int id, double fallback)
{
  char buf[64] = {};
  GetWindowTextA(GetDlgItem(hwnd, id), buf, static_cast<int>(sizeof(buf)));
  char* end = nullptr;
  const double x = std::strtod(buf, &end);
  return (end && end != buf && std::isfinite(x)) ? x : fallback;
}

int PercentToSlider(double x) { return static_cast<int>(std::lround(x * 100.0)); }

void UpdateSliderLabels(HWND hwnd)
{
  for (int id : {kAmbience, kWidth, kSpectralIntelligence, kFrontLock})
  {
    HWND control = GetDlgItem(hwnd, id);
    if (control)
      InvalidateRect(control, nullptr, FALSE);
  }
}

std::array<float, 5> StageDistances(const SettingsState& s)
{
  return {{
      static_cast<float>(s.fl),
      static_cast<float>(s.c),
      static_cast<float>(s.fr),
      static_cast<float>(s.sl),
      static_cast<float>(s.sr),
  }};
}

void PushStateToControls(HWND hwnd, const SettingsState& s)
{
  CheckDlgButton(hwnd, kEnable, s.enabled ? BST_CHECKED : BST_UNCHECKED);

  SendDlgItemMessageW(hwnd, kAmbience, TBM_SETPOS, TRUE, PercentToSlider(s.ambience));
  SendDlgItemMessageW(hwnd, kWidth, TBM_SETPOS, TRUE, PercentToSlider(s.width));
  SendDlgItemMessageW(hwnd, kSpectralIntelligence, TBM_SETPOS, TRUE,
                      PercentToSlider(s.spectralIntelligence));
  SendDlgItemMessageW(hwnd, kFrontLock, TBM_SETPOS, TRUE, PercentToSlider(s.frontLock));

  SetDoubleEdit(hwnd, kPerBinRouting, s.perBinRouting, 2);
  SetDoubleEdit(hwnd, kDimension, s.dimension, 2);
  SetDoubleEdit(hwnd, kCenterWidth, s.centerWidth, 2);
  SetDoubleEdit(hwnd, kReject, s.directReject, 2);
  SetDoubleEdit(hwnd, kCenter, s.center, 2);
  SetDoubleEdit(hwnd, kLowWeight, s.lowWeight, 2);
  SetDoubleEdit(hwnd, kMidWeight, s.midWeight, 2);
  SetDoubleEdit(hwnd, kHighWeight, s.highWeight, 2);

  SetDoubleEdit(hwnd, kAmbienceAttack, s.ambienceAttackMs, 0);
  SetDoubleEdit(hwnd, kAmbienceRelease, s.ambienceReleaseMs, 0);
  SetDoubleEdit(hwnd, kSpatialBinThreshold, s.spatialBinThreshold, 2);
  SetDoubleEdit(hwnd, kSpectralAcquire, s.spectralAcquireMs, 0);
  SetDoubleEdit(hwnd, kSpectralRelease, s.spectralReleaseMs, 0);
  SetDoubleEdit(hwnd, kSteerLow, s.steerLow, 2);
  SetDoubleEdit(hwnd, kSteerBody, s.steerBody, 2);
  SetDoubleEdit(hwnd, kSteerPresence, s.steerPresence, 2);
  SetDoubleEdit(hwnd, kSteerAir, s.steerAir, 2);
  SetDoubleEdit(hwnd, kLockLow, s.lockLow, 2);
  SetDoubleEdit(hwnd, kLockBody, s.lockBody, 2);
  SetDoubleEdit(hwnd, kLockPresence, s.lockPresence, 2);
  SetDoubleEdit(hwnd, kLockAir, s.lockAir, 2);
  SetDoubleEdit(hwnd, kDiffuseThreshold, s.diffuseThreshold, 2);
  SetDoubleEdit(hwnd, kRearBudget, s.rearBudget, 2);
  SetDoubleEdit(hwnd, kDirectThreshold, s.directThreshold, 2);
  SetDoubleEdit(hwnd, kDirectRecovery, s.directRecoveryMs, 0);

  SetDoubleEdit(hwnd, kCenterHp, s.centerHp, 0);
  SetDoubleEdit(hwnd, kCenterLp, s.centerLp, 0);
  SetDoubleEdit(hwnd, kRearHp, s.rearHp, 0);
  SetDoubleEdit(hwnd, kRearLp, s.rearLp, 0);
  SetDoubleEdit(hwnd, kRearLeftTrim, s.rearLeftTrim, 2);
  SetDoubleEdit(hwnd, kRearRightTrim, s.rearRightTrim, 2);

  SetDoubleEdit(hwnd, kFl, s.fl);
  SetDoubleEdit(hwnd, kC, s.c);
  SetDoubleEdit(hwnd, kFr, s.fr);
  SetDoubleEdit(hwnd, kSl, s.sl);
  SetDoubleEdit(hwnd, kSr, s.sr);

  UpdateSliderLabels(hwnd);
  UpdateOhlStageVisual(GetDlgItem(hwnd, kStageViz), StageDistances(s), gLastUiMetrics);
}

void PullControlsToState(HWND hwnd, SettingsState& s)
{
  s.enabled = IsDlgButtonChecked(hwnd, kEnable) == BST_CHECKED;
  s.ambience =
      static_cast<double>(SendDlgItemMessageW(hwnd, kAmbience, TBM_GETPOS, 0, 0)) / 100.0;
  s.width =
      static_cast<double>(SendDlgItemMessageW(hwnd, kWidth, TBM_GETPOS, 0, 0)) / 100.0;
  s.spectralIntelligence =
      static_cast<double>(SendDlgItemMessageW(hwnd, kSpectralIntelligence, TBM_GETPOS, 0, 0)) / 100.0;
  s.perBinRouting =
      std::clamp(GetDoubleEdit(hwnd, kPerBinRouting, s.perBinRouting), 0.0, 1.0);
  s.dimension =
      std::clamp(GetDoubleEdit(hwnd, kDimension, s.dimension), -1.0, 1.0);
  s.centerWidth =
      std::clamp(GetDoubleEdit(hwnd, kCenterWidth, s.centerWidth), 0.0, 1.0);
  s.spatialBinThreshold =
      std::clamp(GetDoubleEdit(hwnd, kSpatialBinThreshold, s.spatialBinThreshold), 0.0, 1.0);
  s.frontLock =
      static_cast<double>(SendDlgItemMessageW(hwnd, kFrontLock, TBM_GETPOS, 0, 0)) / 100.0;
  s.diffuseThreshold =
      std::clamp(GetDoubleEdit(hwnd, kDiffuseThreshold, s.diffuseThreshold), 0.0, 0.95);
  s.rearBudget =
      std::clamp(GetDoubleEdit(hwnd, kRearBudget, s.rearBudget), 0.02, 1.0);
  s.directReject =
      std::clamp(GetDoubleEdit(hwnd, kReject, s.directReject), 0.0, 1.0);
  s.center =
      std::clamp(GetDoubleEdit(hwnd, kCenter, s.center), 0.0, 1.0);

  s.lowWeight =
      std::clamp(GetDoubleEdit(hwnd, kLowWeight, s.lowWeight), 0.0, 1.5);
  s.midWeight =
      std::clamp(GetDoubleEdit(hwnd, kMidWeight, s.midWeight), 0.0, 1.5);
  s.highWeight =
      std::clamp(GetDoubleEdit(hwnd, kHighWeight, s.highWeight), 0.0, 1.5);

  s.ambienceAttackMs = std::clamp(GetDoubleEdit(hwnd, kAmbienceAttack, s.ambienceAttackMs), 5.0, 5000.0);
  s.ambienceReleaseMs = std::clamp(GetDoubleEdit(hwnd, kAmbienceRelease, s.ambienceReleaseMs), 10.0, 10000.0);
  s.spectralAcquireMs = std::clamp(GetDoubleEdit(hwnd, kSpectralAcquire, s.spectralAcquireMs), 5.0, 5000.0);
  s.spectralReleaseMs = std::clamp(GetDoubleEdit(hwnd, kSpectralRelease, s.spectralReleaseMs), 10.0, 10000.0);
  s.steerLow = std::clamp(GetDoubleEdit(hwnd, kSteerLow, s.steerLow), 0.0, 4.0);
  s.steerBody = std::clamp(GetDoubleEdit(hwnd, kSteerBody, s.steerBody), 0.0, 4.0);
  s.steerPresence = std::clamp(GetDoubleEdit(hwnd, kSteerPresence, s.steerPresence), 0.0, 4.0);
  s.steerAir = std::clamp(GetDoubleEdit(hwnd, kSteerAir, s.steerAir), 0.0, 4.0);
  s.lockLow = std::clamp(GetDoubleEdit(hwnd, kLockLow, s.lockLow), 0.0, 2.0);
  s.lockBody = std::clamp(GetDoubleEdit(hwnd, kLockBody, s.lockBody), 0.0, 2.0);
  s.lockPresence = std::clamp(GetDoubleEdit(hwnd, kLockPresence, s.lockPresence), 0.0, 2.0);
  s.lockAir = std::clamp(GetDoubleEdit(hwnd, kLockAir, s.lockAir), 0.0, 2.0);
  s.directThreshold = std::clamp(GetDoubleEdit(hwnd, kDirectThreshold, s.directThreshold), 1.01, 8.0);
  s.directRecoveryMs = std::clamp(GetDoubleEdit(hwnd, kDirectRecovery, s.directRecoveryMs), 1.0, 500.0);

  s.centerHp = std::clamp(GetDoubleEdit(hwnd, kCenterHp, s.centerHp), 200.0, 12000.0);
  s.centerLp = std::clamp(GetDoubleEdit(hwnd, kCenterLp, s.centerLp), 1000.0, 24000.0);
  if (s.centerLp <= s.centerHp) s.centerLp = (std::min)(24000.0, s.centerHp + 500.0);

  s.rearHp = std::clamp(GetDoubleEdit(hwnd, kRearHp, s.rearHp), 0.0, 2000.0);
  s.rearLp = std::clamp(GetDoubleEdit(hwnd, kRearLp, s.rearLp), 1000.0, 24000.0);
  if (s.rearLp <= s.rearHp) s.rearLp = (std::min)(24000.0, s.rearHp + 500.0);

  s.rearLeftTrim = std::clamp(GetDoubleEdit(hwnd, kRearLeftTrim, s.rearLeftTrim), 0.0, 2.0);
  s.rearRightTrim = std::clamp(GetDoubleEdit(hwnd, kRearRightTrim, s.rearRightTrim), 0.0, 2.0);

  s.fl = std::clamp(GetDoubleEdit(hwnd, kFl, s.fl), 0.0, 300.0);
  s.c = std::clamp(GetDoubleEdit(hwnd, kC, s.c), 0.0, 300.0);
  s.fr = std::clamp(GetDoubleEdit(hwnd, kFr, s.fr), 0.0, 300.0);
  s.sl = std::clamp(GetDoubleEdit(hwnd, kSl, s.sl), 0.0, 300.0);
  s.sr = std::clamp(GetDoubleEdit(hwnd, kSr, s.sr), 0.0, 300.0);
}

void SetPreset(SettingsState& s, int preset)
{
  // Geometry is intentionally never changed by a sound preset.
  switch (preset)
  {
    case kPresetNatural:
      s.ambience = 0.56;
      s.width = 0.11;
      s.lowWeight = 0.03;
      s.midWeight = 0.44;
      s.highWeight = 0.53;
      s.ambienceAttackMs = 125;
      s.ambienceReleaseMs = 650;
      s.spectralIntelligence = 0.95;
      s.spatialBinThreshold = 0.34;
      s.perBinRouting = 0.62;
      s.spectralAcquireMs = 80;
      s.spectralReleaseMs = 650;
      s.dimension = -0.12;
      s.centerWidth = 1.00;
      s.steerLow = 0.12; s.steerBody = 0.42; s.steerPresence = 0.78; s.steerAir = 1.05;
      s.lockLow = 0.35; s.lockBody = 1.10; s.lockPresence = 0.90; s.lockAir = 0.25;
      s.frontLock = 0.94;
      s.diffuseThreshold = 0.14;
      s.rearBudget = 0.18;
      s.directReject = 0.58;
      s.directThreshold = 1.35;
      s.directRecoveryMs = 14;
      s.center = 0.12;
      s.centerHp = 2800;
      s.centerLp = 11500;
      s.rearHp = 190;
      s.rearLp = 12000;
      s.rearLeftTrim = 1.0;
      s.rearRightTrim = 1.0;
      break;

    case kPresetWide:
      s.ambience = 0.72;
      s.width = 0.22;
      s.lowWeight = 0.05;
      s.midWeight = 0.45;
      s.highWeight = 0.50;
      s.ambienceAttackMs = 90;
      s.ambienceReleaseMs = 520;
      s.spectralIntelligence = 0.88;
      s.spatialBinThreshold = 0.26;
      s.perBinRouting = 0.58;
      s.spectralAcquireMs = 55;
      s.spectralReleaseMs = 480;
      s.dimension = 0.10;
      s.centerWidth = 0.95;
      s.steerLow = 0.18; s.steerBody = 0.60; s.steerPresence = 1.00; s.steerAir = 1.15;
      s.lockLow = 0.28; s.lockBody = 0.95; s.lockPresence = 0.72; s.lockAir = 0.20;
      s.frontLock = 0.88;
      s.diffuseThreshold = 0.08;
      s.rearBudget = 0.26;
      s.directReject = 0.55;
      s.directThreshold = 1.45;
      s.directRecoveryMs = 18;
      s.center = 0.15;
      s.centerHp = 2500;
      s.centerLp = 14500;
      s.rearHp = 160;
      s.rearLp = 15000;
      s.rearLeftTrim = 1.0;
      s.rearRightTrim = 1.0;
      break;

    case kPresetAmbient:
      s.ambience = 0.86;
      s.width = 0.09;
      s.lowWeight = 0.02;
      s.midWeight = 0.36;
      s.highWeight = 0.62;
      s.ambienceAttackMs = 170;
      s.ambienceReleaseMs = 900;
      s.spectralIntelligence = 1.00;
      s.spatialBinThreshold = 0.24;
      s.perBinRouting = 0.78;
      s.spectralAcquireMs = 90;
      s.spectralReleaseMs = 850;
      s.dimension = 0.24;
      s.centerWidth = 1.00;
      s.steerLow = 0.08; s.steerBody = 0.42; s.steerPresence = 0.95; s.steerAir = 1.30;
      s.lockLow = 0.38; s.lockBody = 1.15; s.lockPresence = 0.92; s.lockAir = 0.18;
      s.frontLock = 0.96;
      s.diffuseThreshold = 0.06;
      s.rearBudget = 0.28;
      s.directReject = 0.65;
      s.directThreshold = 1.22;
      s.directRecoveryMs = 11;
      s.center = 0.09;
      s.centerHp = 3200;
      s.centerLp = 10000;
      s.rearHp = 220;
      s.rearLp = 9500;
      s.rearLeftTrim = 1.0;
      s.rearRightTrim = 1.0;
      break;

    case kPresetV03:
    default:
      s.ambience = 0.70;
      s.width = 0.16;
      s.lowWeight = 0.08;
      s.midWeight = 0.46;
      s.highWeight = 0.46;
      s.ambienceAttackMs = 100;
      s.ambienceReleaseMs = 520;
      s.spectralIntelligence = 0.90;
      s.spatialBinThreshold = 0.30;
      s.perBinRouting = 0.55;
      s.spectralAcquireMs = 65;
      s.spectralReleaseMs = 520;
      s.dimension = 0.00;
      s.centerWidth = 1.00;
      s.steerLow = 0.18; s.steerBody = 0.55; s.steerPresence = 0.90; s.steerAir = 1.10;
      s.lockLow = 0.30; s.lockBody = 1.00; s.lockPresence = 0.82; s.lockAir = 0.25;
      s.frontLock = 0.90;
      s.diffuseThreshold = 0.10;
      s.rearBudget = 0.22;
      s.directReject = 0.60;
      s.directThreshold = 1.45;
      s.directRecoveryMs = 18;
      s.center = 0.18;
      s.centerHp = 2400;
      s.centerLp = 16000;
      s.rearHp = 160;
      s.rearLp = 18000;
      s.rearLeftTrim = 1.0;
      s.rearRightTrim = 1.0;
      break;
  }
}

void Apply(HWND hwnd, SettingsState& state)
{
  PullControlsToState(hwnd, state);
  PushStateToControls(hwnd, state); // reflect clamped values

  std::map<std::string, std::string> values;
  values["stereo_processing"] = state.enabled ? "music" : "receiver";
  values["music_surround_gain"] = Fmt(state.ambience);
  values["music_width_floor"] = Fmt(state.width);
  values["music_ambience_low_weight"] = Fmt(state.lowWeight);
  values["music_ambience_mid_weight"] = Fmt(state.midWeight);
  values["music_ambience_high_weight"] = Fmt(state.highWeight);
  values["music_ambience_attack_ms"] = Fmt(state.ambienceAttackMs, 0);
  values["music_ambience_release_ms"] = Fmt(state.ambienceReleaseMs, 0);
  values["music_spectral_intelligence"] = Fmt(state.spectralIntelligence);
  values["music_spatial_bin_threshold"] = Fmt(state.spatialBinThreshold);
  values["music_per_bin_routing"] = Fmt(state.perBinRouting);
  values["music_spectral_acquire_ms"] = Fmt(state.spectralAcquireMs, 0);
  values["music_spectral_release_ms"] = Fmt(state.spectralReleaseMs, 0);
  values["music_dimension"] = Fmt(state.dimension);
  values["music_center_width"] = Fmt(state.centerWidth);
  values["music_steering_low"] = Fmt(state.steerLow);
  values["music_steering_body"] = Fmt(state.steerBody);
  values["music_steering_presence"] = Fmt(state.steerPresence);
  values["music_steering_air"] = Fmt(state.steerAir);
  values["music_front_lock_low"] = Fmt(state.lockLow);
  values["music_front_lock_body"] = Fmt(state.lockBody);
  values["music_front_lock_presence"] = Fmt(state.lockPresence);
  values["music_front_lock_air"] = Fmt(state.lockAir);
  values["music_front_lock"] = Fmt(state.frontLock);
  values["music_diffuse_threshold"] = Fmt(state.diffuseThreshold);
  values["music_rear_budget"] = Fmt(state.rearBudget);
  values["music_direct_reject"] = Fmt(state.directReject);
  values["music_direct_threshold"] = Fmt(state.directThreshold);
  values["music_direct_recovery_ms"] = Fmt(state.directRecoveryMs, 0);
  values["music_center_treble_gain"] = Fmt(state.center);
  values["music_center_treble_hz"] = Fmt(state.centerHp, 0);
  values["music_center_lowpass_hz"] = Fmt(state.centerLp, 0);
  values["music_rear_highpass_hz"] = Fmt(state.rearHp, 0);
  values["music_rear_lowpass_hz"] = Fmt(state.rearLp, 0);
  values["music_rear_left_trim"] = Fmt(state.rearLeftTrim);
  values["music_rear_right_trim"] = Fmt(state.rearRightTrim);
  values["music_distance_fl_in"] = Fmt(state.fl, 1);
  values["music_distance_c_in"] = Fmt(state.c, 1);
  values["music_distance_fr_in"] = Fmt(state.fr, 1);
  values["music_distance_sl_in"] = Fmt(state.sl, 1);
  values["music_distance_sr_in"] = Fmt(state.sr, 1);

  if (!WriteValues(state.configPath, values))
  {
    SetDlgItemTextW(hwnd, kStatus, L"Could not write config.");
    MessageBoxW(hwnd, L"Could not save the OHL configuration file.",
                L"OHL Music Settings", MB_OK | MB_ICONERROR);
    return;
  }

  std::string response;
  if (SendModeCommand("reload", response, 2000))
    SetDlgItemTextW(hwnd, kStatus, L"Applied live. Surround pipeline reloading...");
  else
    SetDlgItemTextW(hwnd, kStatus, L"Saved. Engine is offline; settings apply next start.");
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE)
  {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    state = static_cast<SettingsState*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }

  switch (msg)
  {
    case WM_CREATE:
    {
      if (!state) return -1;

      gMixControls.clear();
      gLabControls.clear();
      gCardRects.clear();

      gTitleFont = CreateFontW(-29, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      gUiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      gSmallFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

      const auto mix = [&](HWND control) -> HWND {
        if (control) gMixControls.push_back(control);
        return control;
      };
      const auto lab = [&](HWND control) -> HWND {
        if (control) gLabControls.push_back(control);
        return control;
      };

      HWND title = CreateWindowW(
          L"STATIC", L"OHL  |  MUSIC SPATIAL LAB",
          WS_CHILD | WS_VISIBLE,
          20, 13, 720, 36, hwnd, nullptr, nullptr, nullptr);
      SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(gTitleFont), TRUE);

      Label(
          hwnd,
          L"v0.11.3 scene console  \u2014  hardware-good v0.11.2 DSP, rebuilt around listening.",
          21, 49, 810, 21, true);

      HWND enable = Button(hwnd, kEnable, L"OHL MUSIC", 20, 76, 125, 32);
      SendMessageW(enable, BM_SETCHECK, state->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
      Label(hwnd, L"stereo spatial engine", 158, 82, 165, 22, true);

      Button(hwnd, kViewMix, L"MIX", 980, 72, 76, 34);
      Button(hwnd, kViewLab, L"LAB", 1064, 72, 76, 34);

      // The stage is the common visual anchor in both views.
      HWND stage = CreateOhlStageVisual(hwnd, kStageViz, 540, 110, 610, 355);
      SendMessageW(stage, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);

      // ---------------------------------------------------------------------------------------
      // MIX: four ear-facing macros, presets/A-B, stage, analyzer.
      // ---------------------------------------------------------------------------------------
      mix(Knob(
          hwnd, kAmbience,
          L"AMBIENCE\nRoom and diffuse energy allowed to bloom behind you.",
          20, 110, 245, 230, 120, PercentToSlider(state->ambience),
          RGB(88, 181, 255)));
      mix(Knob(
          hwnd, kWidth,
          L"WIDTH\nStable side bed that keeps the stage breathing.",
          275, 110, 245, 230, 60, PercentToSlider(state->width),
          RGB(86, 214, 154)));
      mix(Knob(
          hwnd, kSpectralIntelligence,
          L"INTELLIGENCE\nHow aggressively OHL recognizes spatial cues.",
          20, 350, 245, 230, 100, PercentToSlider(state->spectralIntelligence),
          RGB(241, 187, 84)));
      mix(Knob(
          hwnd, kFrontLock,
          L"FRONT LOCK\nKeeps vocals and direct material anchored up front.",
          275, 350, 245, 230, 100, PercentToSlider(state->frontLock),
          RGB(173, 126, 255)));

      mix(Group(hwnd, L"Quick starts / ear A-B", 20, 590, 500, 190));
      mix(Label(hwnd, L"PRESETS", 36, 620, 72, 20, true));
      mix(Button(hwnd, kPresetNatural, L"NATURAL", 112, 614, 86, 32));
      mix(Button(hwnd, kPresetWide, L"WIDE", 205, 614, 70, 32));
      mix(Button(hwnd, kPresetAmbient, L"AMBIENT", 282, 614, 90, 32));
      mix(Button(hwnd, kPresetV03, L"BASE", 379, 614, 72, 32));

      mix(Label(hwnd, L"COMPARE", 36, 671, 72, 20, true));
      mix(Button(hwnd, kStoreA, L"STORE A", 112, 665, 86, 32));
      mix(Button(hwnd, kRecallA, L"A  \u25B6", 205, 665, 70, 32));
      mix(Button(hwnd, kStoreB, L"STORE B", 282, 665, 90, 32));
      mix(Button(hwnd, kRecallB, L"B  \u25B6", 379, 665, 72, 32));
      mix(Button(hwnd, kSurroundWizard, L"SURROUND WIZARD", 36, 712, 150, 32));
      mix(Button(hwnd, kCapture, L"CAPTURE 15s", 196, 712, 120, 32));
      HWND captureState = CreateWindowW(
          kOhlCardLabelClass,
          L"READY",
          WS_CHILD | WS_VISIBLE,
          329, 718, 146, 22,
          hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCaptureState)),
          nullptr,
          nullptr);
      SendMessageW(
          captureState,
          WM_SETFONT,
          reinterpret_cast<WPARAM>(gSmallFont),
          TRUE);
      mix(captureState);
      mix(Label(
          hwnd,
          L"A/B recall is live. Capture writes exact stereo in, rendered 5.1 out and packet telemetry.",
          36, 751, 445, 20, true));

      HWND analyzer = mix(CreateOhlAnalyzerVisual(hwnd, kAnalyzerViz, 540, 477, 610, 303));
      SendMessageW(analyzer, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);

      // ---------------------------------------------------------------------------------------
      // LAB: every engineering control remains available, but it no longer owns the default UI.
      // ---------------------------------------------------------------------------------------
      lab(Group(hwnd, L"Scene recognition", 20, 110, 500, 245));
      lab(Label(hwnd, L"Band emphasis", 38, 142, 105, 22));
      lab(Label(hwnd, L"LOW", 153, 142, 35, 22, true));
      lab(Edit(hwnd, kLowWeight, 188, 138, 60));
      lab(Label(hwnd, L"MID", 269, 142, 35, 22, true));
      lab(Edit(hwnd, kMidWeight, 304, 138, 60));
      lab(Label(hwnd, L"HIGH", 385, 142, 40, 22, true));
      lab(Edit(hwnd, kHighWeight, 426, 138, 60));

      lab(Label(hwnd, L"Spatial selectivity", 38, 184, 120, 22));
      lab(Edit(hwnd, kSpatialBinThreshold, 161, 180, 66));
      lab(Label(hwnd, L"broad attack", 255, 184, 86, 22, true));
      lab(Edit(hwnd, kAmbienceAttack, 343, 180, 62));
      lab(Label(hwnd, L"release", 412, 184, 48, 22, true));
      lab(Edit(hwnd, kAmbienceRelease, 462, 180, 48));

      lab(Label(hwnd, L"Ownership attack", 38, 226, 118, 22));
      lab(Edit(hwnd, kSpectralAcquire, 161, 222, 66));
      lab(Label(hwnd, L"release", 255, 226, 52, 22, true));
      lab(Edit(hwnd, kSpectralRelease, 309, 222, 66));
      lab(Label(hwnd, L"ms", 380, 226, 28, 22, true));

      lab(Label(
          hwnd,
          L"Hard ownership is selective; soft ownership rescues subtle presence/air without promoting the whole mix.",
          38, 272, 444, 55, true));

      lab(Group(hwnd, L"Transient / matrix behavior", 20, 365, 500, 415));
      lab(Label(hwnd, L"Diffuse gate", 38, 398, 85, 22));
      lab(Edit(hwnd, kDiffuseThreshold, 126, 394, 66));
      lab(Label(hwnd, L"Rear budget", 222, 398, 82, 22));
      lab(Edit(hwnd, kRearBudget, 307, 394, 66));
      lab(Label(hwnd, L"Transient protect", 38, 440, 120, 22));
      lab(Edit(hwnd, kReject, 161, 436, 66));

      lab(Label(hwnd, L"Event sensitivity", 255, 440, 105, 22));
      lab(Edit(hwnd, kDirectThreshold, 362, 436, 66));
      lab(Label(hwnd, L"Recovery", 38, 482, 70, 22));
      lab(Edit(hwnd, kDirectRecovery, 111, 478, 66));
      lab(Label(hwnd, L"ms", 181, 482, 28, 22, true));

      lab(Label(hwnd, L"Per-bin mix", 255, 482, 82, 22));
      lab(Edit(hwnd, kPerBinRouting, 340, 478, 66));
      lab(Label(hwnd, L"Dimension", 38, 524, 75, 22));
      lab(Edit(hwnd, kDimension, 116, 520, 66));
      lab(Label(hwnd, L"Center width", 222, 524, 92, 22));
      lab(Edit(hwnd, kCenterWidth, 317, 520, 66));

      lab(Label(hwnd, L"Center sparkle", 38, 568, 105, 22));
      lab(Edit(hwnd, kCenter, 146, 564, 66));
      lab(Label(hwnd, L"Center HP", 255, 568, 75, 22));
      lab(Edit(hwnd, kCenterHp, 333, 564, 72));
      lab(Label(hwnd, L"LP", 413, 568, 26, 22, true));
      lab(Edit(hwnd, kCenterLp, 441, 564, 69));

      lab(Label(hwnd, L"Rear HP", 38, 610, 65, 22));
      lab(Edit(hwnd, kRearHp, 106, 606, 70));
      lab(Label(hwnd, L"Rear LP", 222, 610, 65, 22));
      lab(Edit(hwnd, kRearLp, 290, 606, 80));
      lab(Label(hwnd, L"SL trim", 38, 652, 62, 22));
      lab(Edit(hwnd, kRearLeftTrim, 103, 648, 66));
      lab(Label(hwnd, L"SR trim", 222, 652, 62, 22));
      lab(Edit(hwnd, kRearRightTrim, 287, 648, 66));

      lab(Label(
          hwnd,
          L"High-frequency transients now spare the stable width bed; direct-event protection acts on the adaptive layer.",
          38, 700, 444, 52, true));

      lab(Group(hwnd, L"Per-band steering / geometry", 540, 477, 610, 303));
      lab(Label(hwnd, L"Band", 562, 511, 50, 22, true));
      lab(Label(hwnd, L"LOW", 633, 511, 50, 22, true));
      lab(Label(hwnd, L"BODY", 721, 511, 55, 22, true));
      lab(Label(hwnd, L"PRES", 814, 511, 55, 22, true));
      lab(Label(hwnd, L"AIR", 909, 511, 50, 22, true));

      lab(Label(hwnd, L"Steer", 562, 547, 58, 22));
      lab(Edit(hwnd, kSteerLow, 628, 543, 62));
      lab(Edit(hwnd, kSteerBody, 716, 543, 62));
      lab(Edit(hwnd, kSteerPresence, 809, 543, 62));
      lab(Edit(hwnd, kSteerAir, 904, 543, 62));

      lab(Label(hwnd, L"Front lock", 562, 588, 70, 22));
      lab(Edit(hwnd, kLockLow, 628, 584, 62));
      lab(Edit(hwnd, kLockBody, 716, 584, 62));
      lab(Edit(hwnd, kLockPresence, 809, 584, 62));
      lab(Edit(hwnd, kLockAir, 904, 584, 62));

      lab(Label(hwnd, L"Geometry", 562, 638, 70, 22));
      lab(Label(hwnd, L"FL", 635, 638, 22, 22, true));
      lab(Edit(hwnd, kFl, 658, 634, 62));
      lab(Label(hwnd, L"C", 731, 638, 18, 22, true));
      lab(Edit(hwnd, kC, 750, 634, 62));
      lab(Label(hwnd, L"FR", 823, 638, 24, 22, true));
      lab(Edit(hwnd, kFr, 848, 634, 62));
      lab(Label(hwnd, L"SL", 921, 638, 24, 22, true));
      lab(Edit(hwnd, kSl, 946, 634, 62));
      lab(Label(hwnd, L"SR", 1019, 638, 24, 22, true));
      lab(Edit(hwnd, kSr, 1044, 634, 62));

      lab(Label(
          hwnd,
          L"Distances are mirrored in the stage above. You can also drag any speaker there; edits stay unapplied until APPLY LIVE.",
          562, 690, 552, 56, true));

      // Always-visible transport / status strip.
      HWND apply = Button(hwnd, kApply, L"APPLY LIVE", 20, 804, 156, 42);
      SendMessageW(apply, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
      Button(hwnd, kReload, L"RELOAD SAVED", 184, 804, 145, 42);

      HWND status = CreateWindowW(
          L"STATIC",
          L"MIX view \u2014 tune by ear; LAB holds the engineering controls.",
          WS_CHILD | WS_VISIBLE,
          350, 815, 790, 28, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatus)),
          nullptr, nullptr);
      SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);

      PushStateToControls(hwnd, *state);
      SetUiPage(hwnd, true);
      StartMetricsWorker(hwnd);
      return 0;
    }

    case OHL_STAGE_DISTANCE_CHANGED:
    {
      if (!state) return 0;
      const int speaker = static_cast<int>(wp);
      const double inches = std::clamp(static_cast<double>(lp) / 100.0, 12.0, 120.0);

      switch (static_cast<OhlStageSpeaker>(speaker))
      {
        case OhlStageSpeaker::FL: state->fl = inches; SetDoubleEdit(hwnd, kFl, inches); break;
        case OhlStageSpeaker::C:  state->c  = inches; SetDoubleEdit(hwnd, kC, inches); break;
        case OhlStageSpeaker::FR: state->fr = inches; SetDoubleEdit(hwnd, kFr, inches); break;
        case OhlStageSpeaker::SL: state->sl = inches; SetDoubleEdit(hwnd, kSl, inches); break;
        case OhlStageSpeaker::SR: state->sr = inches; SetDoubleEdit(hwnd, kSr, inches); break;
      }

      UpdateOhlStageVisual(GetDlgItem(hwnd, kStageViz), StageDistances(*state), gLastUiMetrics);
      SetDlgItemTextW(hwnd, kStatus, L"Speaker distance changed on stage. Press APPLY LIVE to audition geometry.");
      return 0;
    }

    case WM_HSCROLL:
      UpdateSliderLabels(hwnd);
      return 0;

    case WM_COMMAND:
    {
      const int id = LOWORD(wp);
      const int notification = HIWORD(wp);
      if (!state) break;

      if (notification == EN_CHANGE &&
          (id == kFl || id == kC || id == kFr || id == kSl || id == kSr))
      {
        if (id == kFl) state->fl = std::clamp(GetDoubleEdit(hwnd, kFl, state->fl), 0.0, 300.0);
        if (id == kC)  state->c  = std::clamp(GetDoubleEdit(hwnd, kC, state->c), 0.0, 300.0);
        if (id == kFr) state->fr = std::clamp(GetDoubleEdit(hwnd, kFr, state->fr), 0.0, 300.0);
        if (id == kSl) state->sl = std::clamp(GetDoubleEdit(hwnd, kSl, state->sl), 0.0, 300.0);
        if (id == kSr) state->sr = std::clamp(GetDoubleEdit(hwnd, kSr, state->sr), 0.0, 300.0);
        UpdateOhlStageVisual(GetDlgItem(hwnd, kStageViz), StageDistances(*state), gLastUiMetrics);
      }

      if (id == kViewMix)
      {
        SetUiPage(hwnd, true);
        return 0;
      }
      if (id == kViewLab)
      {
        SetUiPage(hwnd, false);
        return 0;
      }
      if (id == kEnable)
      {
        const LRESULT checked = SendDlgItemMessageW(hwnd, kEnable, BM_GETCHECK, 0, 0);
        SendDlgItemMessageW(
            hwnd, kEnable, BM_SETCHECK,
            checked == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
        SetDlgItemTextW(hwnd, kStatus, L"OHL Music enable state changed. Press APPLY LIVE to commit.");
        return 0;
      }

      if (id == kSurroundWizard)
      {
        LaunchSurroundWizard();
        SetDlgItemTextW(hwnd, kStatus, L"Surround Wizard opened — diagnostic tones bypass OHL Music while active.");
        return 0;
      }

      if (id == kCapture)
      {
        std::string current;
        const bool online = SendModeCommand("capture status", current, 300);
        const auto fields = online
            ? ParseCompactFields(current)
            : std::map<std::string, std::string>{};
        const auto stateIt = fields.find("capture_state");
        const std::string captureState =
            stateIt == fields.end() ? std::string() : stateIt->second;
        const bool active =
            captureState == "armed" || captureState == "recording" ||
            captureState == "complete" || captureState == "saving";

        std::string response;
        if (!SendModeCommand(
                active ? "capture cancel" : "capture start 15",
                response,
                1000))
        {
          response = "capture_state=error;error=engine_offline";
        }
        UpdateCaptureUi(hwnd, response);
        return 0;
      }

      if (id == kApply)
      {
        Apply(hwnd, *state);
        return 0;
      }
      if (id == kReload)
      {
        SettingsState loaded;
        loaded.configPath = state->configPath;
        LoadState(loaded);
        *state = loaded;
        PushStateToControls(hwnd, *state);
        SetDlgItemTextW(hwnd, kStatus, L"Reloaded saved config; no audio change yet.");
        return 0;
      }
      if (id == kPresetNatural || id == kPresetWide || id == kPresetAmbient || id == kPresetV03)
      {
        PullControlsToState(hwnd, *state); // retain current geometry + enable state
        SetPreset(*state, id);
        PushStateToControls(hwnd, *state);

        const wchar_t* name = id == kPresetNatural ? L"Natural" :
                              id == kPresetWide ? L"Wide" :
                              id == kPresetAmbient ? L"Ambient" : L"Baseline";
        std::wstring msgText = std::wstring(name) + L" loaded into controls. Press Apply Live to audition.";
        SetDlgItemTextW(hwnd, kStatus, msgText.c_str());
        return 0;
      }
      if (id == kStoreA || id == kStoreB)
      {
        PullControlsToState(hwnd, *state);
        if (id == kStoreA)
        {
          gSnapshotA = *state;
          gHaveSnapshotA = true;
          SetDlgItemTextW(hwnd, kStatus, L"Snapshot A stored from current controls.");
        }
        else
        {
          gSnapshotB = *state;
          gHaveSnapshotB = true;
          SetDlgItemTextW(hwnd, kStatus, L"Snapshot B stored from current controls.");
        }
        return 0;
      }
      if (id == kRecallA || id == kRecallB)
      {
        const bool have = id == kRecallA ? gHaveSnapshotA : gHaveSnapshotB;
        if (!have)
        {
          SetDlgItemTextW(hwnd, kStatus,
                          id == kRecallA ? L"Snapshot A is empty." : L"Snapshot B is empty.");
          return 0;
        }

        const std::string configPath = state->configPath;
        *state = id == kRecallA ? gSnapshotA : gSnapshotB;
        state->configPath = configPath;
        PushStateToControls(hwnd, *state);
        Apply(hwnd, *state);
        SetDlgItemTextW(hwnd, kStatus,
                        id == kRecallA ? L"Snapshot A applied live." : L"Snapshot B applied live.");
        return 0;
      }
      break;
    }

    case kCaptureMessage:
    {
      std::unique_ptr<std::string> captureStatus(
          reinterpret_cast<std::string*>(lp));
      if (captureStatus)
        UpdateCaptureUi(hwnd, *captureStatus);
      return 0;
    }

    case kMetricsMessage:
    {
      std::unique_ptr<OhlAnalyzerMetrics> metrics(
          reinterpret_cast<OhlAnalyzerMetrics*>(lp));
      if (!metrics)
        return 0;

      gLastUiMetrics = *metrics;
      UpdateOhlAnalyzerVisual(GetDlgItem(hwnd, kAnalyzerViz), gLastUiMetrics);
      if (state)
        UpdateOhlStageVisual(GetDlgItem(hwnd, kStageViz), StageDistances(*state), gLastUiMetrics);
      return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
      HDC dc = reinterpret_cast<HDC>(wp);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(225, 232, 244));
      return reinterpret_cast<LRESULT>(gBg);
    }

    case WM_CTLCOLOREDIT:
    {
      HDC dc = reinterpret_cast<HDC>(wp);
      SetBkColor(dc, RGB(24, 30, 40));
      SetTextColor(dc, RGB(238, 243, 251));
      return reinterpret_cast<LRESULT>(gEditBg ? gEditBg : gBg);
    }

    case WM_DESTROY:
      StopMetricsWorker(hwnd);
      if (gTitleFont) { DeleteObject(gTitleFont); gTitleFont = nullptr; }
      if (gUiFont) { DeleteObject(gUiFont); gUiFont = nullptr; }
      if (gSmallFont) { DeleteObject(gSmallFont); gSmallFont = nullptr; }
      if (gEditBg) { DeleteObject(gEditBg); gEditBg = nullptr; }
      if (gBg) { DeleteObject(gBg); gBg = nullptr; }
      PostQuitMessage(0);
      return 0;
  }

  return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int RunMusicSettingsGui(const std::string& configPath)
{
  INITCOMMONCONTROLSEX icc = {};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_BAR_CLASSES;
  InitCommonControlsEx(&icc);

  SettingsState state;
  state.configPath = configPath;
  LoadState(state);

  HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!RegisterOhlModernControls(instance) ||
      !RegisterOhlMacroKnob(instance) ||
      !RegisterOhlAnalyzerVisual(instance) ||
      !RegisterOhlStageVisual(instance))
    return 1;

  gBg = CreateSolidBrush(RGB(15, 17, 23));
  gEditBg = CreateSolidBrush(RGB(24, 30, 40));

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = SettingsWndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kClassName;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = GetOhlBrandIcon();
  wc.hIconSm = GetOhlBrandIcon();
  wc.hbrBackground = gBg;
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(
      0, kClassName, L"OHL Music Spatial Lab",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, 1180, 900,
      nullptr, nullptr, instance, &state);

  if (!hwnd)
    return 1;

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);

  MSG msg = {};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}
