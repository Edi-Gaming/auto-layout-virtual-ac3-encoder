#include "MusicSettings.h"
#include "BrandIcon.h"
#include "ModeControl.h"

#include <commctrl.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
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
  kStoreA,
  kRecallA,
  kStoreB,
  kRecallB,
  kMeterSummary,
  kMeterBands,
  kReload,
  kApply,
  kStatus
};

HBRUSH gBg = nullptr;
HFONT gTitleFont = nullptr;
HFONT gUiFont = nullptr;
HFONT gSmallFont = nullptr;

constexpr UINT_PTR kMetricsTimer = 17;
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

void RefreshMetrics(HWND hwnd)
{
  std::string response;
  if (!SendModeCommand("metrics", response, 120))
  {
    SetDlgItemTextW(hwnd, kMeterSummary, L"Engine offline / metrics unavailable.");
    SetDlgItemTextW(hwnd, kMeterBands, L"OWN  --  --  --  --\r\nCTR  --  --  --  --");
    return;
  }

  const auto f = ParseCompactFields(response);
  if (f.empty())
    return;

  const double amb = MetricDouble(f, "amb");
  const double ctr = MetricDouble(f, "center");
  const double bins = MetricDouble(f, "bins");
  const double trans = MetricDouble(f, "trans");
  const double rear = MetricDouble(f, "rear");
  const double lock = MetricDouble(f, "lock");
  const double budget = MetricDouble(f, "budget", 1.0);
  const auto own = MetricQuad(f, "own");
  const auto bandCenter = MetricQuad(f, "bandcenter");

  wchar_t summary[512] = {};
  swprintf_s(summary, L"Ambience %3.0f%%   Center %3.0f%%\r\n"
                      L"Spatial bins %3.0f%%   Transient %3.0f%%\r\n"
                      L"Rear open %3.0f%%   Front lock %3.0f%%\r\n"
                      L"Budget scale %3.0f%%",
             100.0 * amb, 100.0 * ctr, 100.0 * bins, 100.0 * trans,
             100.0 * rear, 100.0 * lock, 100.0 * budget);
  SetDlgItemTextW(hwnd, kMeterSummary, summary);

  wchar_t bands[512] = {};
  swprintf_s(bands, L"          LOW   BODY   PRES    AIR\r\n"
                    L"OWN      %3.0f    %3.0f    %3.0f    %3.0f\r\n"
                    L"CENTER   %3.0f    %3.0f    %3.0f    %3.0f",
             100.0 * own[0], 100.0 * own[1], 100.0 * own[2], 100.0 * own[3],
             100.0 * bandCenter[0], 100.0 * bandCenter[1],
             100.0 * bandCenter[2], 100.0 * bandCenter[3]);
  SetDlgItemTextW(hwnd, kMeterBands, bands);
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

HWND Label(HWND parent, const wchar_t* text, int x, int y, int w, int h, bool useSmallFont = false)
{
  HWND c = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                         x, y, w, h, parent, nullptr, nullptr, nullptr);
  SendMessageW(c, WM_SETFONT,
               reinterpret_cast<WPARAM>(useSmallFont ? gSmallFont : gUiFont), TRUE);
  return c;
}

HWND Group(HWND parent, const wchar_t* text, int x, int y, int w, int h)
{
  HWND c = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                         x, y, w, h, parent, nullptr, nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  return c;
}

HWND Edit(HWND parent, int id, int x, int y, int w)
{
  HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           x, y, w, 24, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  return c;
}

HWND Slider(HWND parent, int id, int x, int y, int w, int maxValue, int pos)
{
  HWND c = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
                         x, y, w, 32, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
  SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(0, maxValue));
  SendMessageW(c, TBM_SETPOS, TRUE, pos);
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
  HWND c = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         x, y, w, h, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  return c;
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
  const auto pct = [&](int slider, int label) {
    const int v = static_cast<int>(SendDlgItemMessageW(hwnd, slider, TBM_GETPOS, 0, 0));
    const std::wstring s = std::to_wstring(v) + L"%";
    SetDlgItemTextW(hwnd, label, s.c_str());
  };
  pct(kAmbience, kAmbienceValue);
  pct(kWidth, kWidthValue);
  pct(kSpectralIntelligence, kSpectralIntelligenceValue);
  pct(kPerBinRouting, kPerBinRoutingValue);
  {
    const int v = static_cast<int>(SendDlgItemMessageW(hwnd, kDimension, TBM_GETPOS, 0, 0));
    const int signedValue = v - 100;
    const std::wstring s = (signedValue > 0 ? L"+" : L"") + std::to_wstring(signedValue) + L"%";
    SetDlgItemTextW(hwnd, kDimensionValue, s.c_str());
  }
  pct(kCenterWidth, kCenterWidthValue);
  pct(kFrontLock, kFrontLockValue);
  pct(kReject, kRejectValue);
  pct(kCenter, kCenterValue);
  pct(kLowWeight, kLowWeightValue);
  pct(kMidWeight, kMidWeightValue);
  pct(kHighWeight, kHighWeightValue);
}

void PushStateToControls(HWND hwnd, const SettingsState& s)
{
  CheckDlgButton(hwnd, kEnable, s.enabled ? BST_CHECKED : BST_UNCHECKED);

  SendDlgItemMessageW(hwnd, kAmbience, TBM_SETPOS, TRUE, PercentToSlider(s.ambience));
  SendDlgItemMessageW(hwnd, kWidth, TBM_SETPOS, TRUE, PercentToSlider(s.width));
  SendDlgItemMessageW(hwnd, kSpectralIntelligence, TBM_SETPOS, TRUE,
                      PercentToSlider(s.spectralIntelligence));
  SendDlgItemMessageW(hwnd, kPerBinRouting, TBM_SETPOS, TRUE,
                      PercentToSlider(s.perBinRouting));
  SendDlgItemMessageW(hwnd, kDimension, TBM_SETPOS, TRUE,
                      static_cast<LPARAM>(std::lround((s.dimension + 1.0) * 100.0)));
  SendDlgItemMessageW(hwnd, kCenterWidth, TBM_SETPOS, TRUE,
                      PercentToSlider(s.centerWidth));
  SendDlgItemMessageW(hwnd, kFrontLock, TBM_SETPOS, TRUE, PercentToSlider(s.frontLock));
  SendDlgItemMessageW(hwnd, kReject, TBM_SETPOS, TRUE, PercentToSlider(s.directReject));
  SendDlgItemMessageW(hwnd, kCenter, TBM_SETPOS, TRUE, PercentToSlider(s.center));

  SendDlgItemMessageW(hwnd, kLowWeight, TBM_SETPOS, TRUE, PercentToSlider(s.lowWeight));
  SendDlgItemMessageW(hwnd, kMidWeight, TBM_SETPOS, TRUE, PercentToSlider(s.midWeight));
  SendDlgItemMessageW(hwnd, kHighWeight, TBM_SETPOS, TRUE, PercentToSlider(s.highWeight));

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
      static_cast<double>(SendDlgItemMessageW(hwnd, kPerBinRouting, TBM_GETPOS, 0, 0)) / 100.0;
  s.dimension =
      (static_cast<double>(SendDlgItemMessageW(hwnd, kDimension, TBM_GETPOS, 0, 0)) - 100.0) / 100.0;
  s.centerWidth =
      static_cast<double>(SendDlgItemMessageW(hwnd, kCenterWidth, TBM_GETPOS, 0, 0)) / 100.0;
  s.spatialBinThreshold =
      std::clamp(GetDoubleEdit(hwnd, kSpatialBinThreshold, s.spatialBinThreshold), 0.0, 1.0);
  s.frontLock =
      static_cast<double>(SendDlgItemMessageW(hwnd, kFrontLock, TBM_GETPOS, 0, 0)) / 100.0;
  s.diffuseThreshold =
      std::clamp(GetDoubleEdit(hwnd, kDiffuseThreshold, s.diffuseThreshold), 0.0, 0.95);
  s.rearBudget =
      std::clamp(GetDoubleEdit(hwnd, kRearBudget, s.rearBudget), 0.02, 1.0);
  s.directReject =
      static_cast<double>(SendDlgItemMessageW(hwnd, kReject, TBM_GETPOS, 0, 0)) / 100.0;
  s.center =
      static_cast<double>(SendDlgItemMessageW(hwnd, kCenter, TBM_GETPOS, 0, 0)) / 100.0;

  s.lowWeight =
      static_cast<double>(SendDlgItemMessageW(hwnd, kLowWeight, TBM_GETPOS, 0, 0)) / 100.0;
  s.midWeight =
      static_cast<double>(SendDlgItemMessageW(hwnd, kMidWeight, TBM_GETPOS, 0, 0)) / 100.0;
  s.highWeight =
      static_cast<double>(SendDlgItemMessageW(hwnd, kHighWeight, TBM_GETPOS, 0, 0)) / 100.0;

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

      gTitleFont = CreateFontW(-27, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      gUiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      gSmallFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

      HWND title = CreateWindowW(L"STATIC", L"OHL  |  MUSIC SPATIAL LAB",
                                 WS_CHILD | WS_VISIBLE,
                                 20, 14, 700, 34, hwnd, nullptr, nullptr, nullptr);
      SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(gTitleFont), TRUE);
      Label(hwnd, L"v0.10 spectral intelligence — per-bin spatial recognition on the clean M/S renderer.",
            21, 48, 710, 21, true);

      HWND enable = CreateWindowW(L"BUTTON", L"Enable OHL Music for stereo",
                                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                  20, 76, 245, 25, hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEnable)),
                                  nullptr, nullptr);
      SendMessageW(enable, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);

      Label(hwnd, L"Quick starts:", 300, 79, 78, 22, true);
      Button(hwnd, kPresetNatural, L"NATURAL", 378, 73, 83);
      Button(hwnd, kPresetWide, L"WIDE", 467, 73, 72);
      Button(hwnd, kPresetAmbient, L"AMBIENT", 545, 73, 88);
      Button(hwnd, kPresetV03, L"BASE", 639, 73, 64);

      Group(hwnd, L"Spatial field", 14, 108, 722, 300);
      Label(hwnd, L"Adaptive ambience", 30, 136, 150, 22);
      Slider(hwnd, kAmbience, 180, 129, 445, 120, PercentToSlider(state->ambience));
      ValueLabel(hwnd, kAmbienceValue, 642, 136);

      Label(hwnd, L"Base side width", 30, 174, 150, 22);
      Slider(hwnd, kWidth, 180, 167, 445, 60, PercentToSlider(state->width));
      ValueLabel(hwnd, kWidthValue, 642, 174);

      Label(hwnd, L"Ambience band emphasis", 30, 210, 160, 22, true);
      Label(hwnd, L"LOW", 34, 237, 42, 20, true);
      Slider(hwnd, kLowWeight, 78, 230, 145, 150, PercentToSlider(state->lowWeight));
      ValueLabel(hwnd, kLowWeightValue, 220, 237, 52);
      Label(hwnd, L"MID", 276, 237, 42, 20, true);
      Slider(hwnd, kMidWeight, 316, 230, 145, 150, PercentToSlider(state->midWeight));
      ValueLabel(hwnd, kMidWeightValue, 458, 237, 52);
      Label(hwnd, L"HIGH", 512, 237, 45, 20, true);
      Slider(hwnd, kHighWeight, 557, 230, 110, 150, PercentToSlider(state->highWeight));
      ValueLabel(hwnd, kHighWeightValue, 670, 237, 52);

      Label(hwnd, L"Spectral intelligence", 30, 282, 150, 22);
      Slider(hwnd, kSpectralIntelligence, 180, 275, 335, 100,
             PercentToSlider(state->spectralIntelligence));
      ValueLabel(hwnd, kSpectralIntelligenceValue, 520, 282, 60);
      Label(hwnd, L"Bin selectivity", 588, 282, 88, 22, true);
      Edit(hwnd, kSpatialBinThreshold, 676, 278, 48);

      Label(hwnd, L"Steering attack", 30, 326, 105, 22);
      Edit(hwnd, kAmbienceAttack, 137, 322, 72);
      Label(hwnd, L"ms", 213, 326, 25, 22, true);
      Label(hwnd, L"release", 265, 326, 55, 22);
      Edit(hwnd, kAmbienceRelease, 323, 322, 72);
      Label(hwnd, L"ms", 399, 326, 25, 22, true);
      Label(hwnd, L"Intelligence = FFT classifier blend; selectivity = spatial-bin confidence threshold.",
            30, 361, 680, 20, true);

      Group(hwnd, L"Front anchoring / direct events", 14, 416, 722, 198);
      Label(hwnd, L"Front / vocal lock", 30, 444, 150, 22);
      Slider(hwnd, kFrontLock, 180, 437, 445, 100, PercentToSlider(state->frontLock));
      ValueLabel(hwnd, kFrontLockValue, 642, 444);

      Label(hwnd, L"Diffuse gate", 30, 485, 88, 22);
      Edit(hwnd, kDiffuseThreshold, 120, 481, 72);
      Label(hwnd, L"0 = always open; higher = only diffuse/room-like material", 201, 485, 360, 22, true);
      Label(hwnd, L"Rear budget", 575, 485, 80, 22);
      Edit(hwnd, kRearBudget, 656, 481, 58);
      Label(hwnd, L"ratio", 716, 485, 35, 22, true);

      Label(hwnd, L"Direct-event reject", 30, 524, 150, 22);
      Slider(hwnd, kReject, 180, 517, 445, 100, PercentToSlider(state->directReject));
      ValueLabel(hwnd, kRejectValue, 642, 524);

      Label(hwnd, L"Sensitivity ratio", 30, 567, 105, 22);
      Edit(hwnd, kDirectThreshold, 137, 563, 72);
      Label(hwnd, L"(lower = catches more)", 214, 567, 130, 22, true);
      Label(hwnd, L"Recovery", 405, 567, 62, 22);
      Edit(hwnd, kDirectRecovery, 470, 563, 72);
      Label(hwnd, L"ms", 546, 567, 25, 22, true);

      Group(hwnd, L"Speaker voicing", 14, 622, 722, 142);
      Label(hwnd, L"Center sparkle", 30, 650, 150, 22);
      Slider(hwnd, kCenter, 180, 643, 445, 50, PercentToSlider(state->center));
      ValueLabel(hwnd, kCenterValue, 642, 650);

      Label(hwnd, L"Center band", 30, 692, 88, 22);
      Edit(hwnd, kCenterHp, 120, 688, 72);
      Label(hwnd, L"—", 197, 692, 15, 22);
      Edit(hwnd, kCenterLp, 215, 688, 76);
      Label(hwnd, L"Hz", 295, 692, 25, 22, true);

      Label(hwnd, L"Rear band", 348, 692, 72, 22);
      Edit(hwnd, kRearHp, 423, 688, 70);
      Label(hwnd, L"—", 497, 692, 15, 22);
      Edit(hwnd, kRearLp, 515, 688, 76);
      Label(hwnd, L"Hz", 595, 692, 25, 22, true);

      Label(hwnd, L"SL trim", 30, 729, 52, 22);
      Edit(hwnd, kRearLeftTrim, 84, 725, 65);
      Label(hwnd, L"SR trim", 178, 729, 52, 22);
      Edit(hwnd, kRearRightTrim, 232, 725, 65);
      Label(hwnd, L"1.00 = unity. Useful for room/speaker asymmetry.", 320, 729, 350, 20, true);

      Group(hwnd, L"Geometry / time alignment", 14, 772, 722, 78);
      Label(hwnd, L"FL", 30, 802, 24, 22); Edit(hwnd, kFl, 54, 798, 58);
      Label(hwnd, L"C", 133, 802, 18, 22); Edit(hwnd, kC, 151, 798, 58);
      Label(hwnd, L"FR", 229, 802, 24, 22); Edit(hwnd, kFr, 253, 798, 58);
      Label(hwnd, L"SL", 331, 802, 24, 22); Edit(hwnd, kSl, 355, 798, 58);
      Label(hwnd, L"SR", 433, 802, 24, 22); Edit(hwnd, kSr, 457, 798, 58);
      Label(hwnd, L"inches from listening position", 530, 802, 175, 22, true);

      HWND apply = Button(hwnd, kApply, L"APPLY LIVE", 20, 868, 150, 42);
      SendMessageW(apply, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
      Button(hwnd, kReload, L"RELOAD SAVED", 180, 868, 145, 42);

      HWND status = CreateWindowW(L"STATIC", L"Presets are non-destructive until Apply Live.",
                                  WS_CHILD | WS_VISIBLE,
                                  344, 878, 380, 30, hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatus)),
                                  nullptr, nullptr);
      SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);

      PushStateToControls(hwnd, *state);
      return 0;
    }

    case WM_HSCROLL:
      UpdateSliderLabels(hwnd);
      return 0;

    case WM_COMMAND:
    {
      const int id = LOWORD(wp);
      if (!state) break;

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
      break;
    }

    case WM_CTLCOLORSTATIC:
    {
      HDC dc = reinterpret_cast<HDC>(wp);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(225, 232, 244));
      return reinterpret_cast<LRESULT>(gBg);
    }

    case WM_DESTROY:
      if (gTitleFont) { DeleteObject(gTitleFont); gTitleFont = nullptr; }
      if (gUiFont) { DeleteObject(gUiFont); gUiFont = nullptr; }
      if (gSmallFont) { DeleteObject(gSmallFont); gSmallFont = nullptr; }
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
  gBg = CreateSolidBrush(RGB(15, 17, 23));

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
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 770, 965,
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
