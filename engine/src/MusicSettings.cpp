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
  kReject,
  kCenter,

  kAmbienceValue,
  kWidthValue,
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
  kReload,
  kApply,
  kStatus
};

HBRUSH gBg = nullptr;
HFONT gTitleFont = nullptr;
HFONT gUiFont = nullptr;
HFONT gSmallFont = nullptr;

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
  SendDlgItemMessageW(hwnd, kReject, TBM_SETPOS, TRUE, PercentToSlider(s.directReject));
  SendDlgItemMessageW(hwnd, kCenter, TBM_SETPOS, TRUE, PercentToSlider(s.center));

  SendDlgItemMessageW(hwnd, kLowWeight, TBM_SETPOS, TRUE, PercentToSlider(s.lowWeight));
  SendDlgItemMessageW(hwnd, kMidWeight, TBM_SETPOS, TRUE, PercentToSlider(s.midWeight));
  SendDlgItemMessageW(hwnd, kHighWeight, TBM_SETPOS, TRUE, PercentToSlider(s.highWeight));

  SetDoubleEdit(hwnd, kAmbienceAttack, s.ambienceAttackMs, 0);
  SetDoubleEdit(hwnd, kAmbienceRelease, s.ambienceReleaseMs, 0);
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
      s.directReject = 0.88;
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
      s.directReject = 0.82;
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
      s.directReject = 0.94;
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
      s.directReject = 0.78;
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
      Label(hwnd, L"v0.4 advanced tuning — presets change controls only; Apply Live commits them.",
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
      Button(hwnd, kPresetV03, L"v0.3", 639, 73, 64);

      Group(hwnd, L"Spatial field", 14, 108, 722, 260);
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

      Label(hwnd, L"Steering attack", 30, 286, 105, 22);
      Edit(hwnd, kAmbienceAttack, 137, 282, 72);
      Label(hwnd, L"ms", 213, 286, 25, 22, true);
      Label(hwnd, L"release", 265, 286, 55, 22);
      Edit(hwnd, kAmbienceRelease, 323, 282, 72);
      Label(hwnd, L"ms", 399, 286, 25, 22, true);
      Label(hwnd, L"Attack = how fast ambience opens; release = how long the room stays open.",
            30, 321, 650, 20, true);

      Group(hwnd, L"Direct events / percussion", 14, 376, 722, 118);
      Label(hwnd, L"Direct-event reject", 30, 404, 150, 22);
      Slider(hwnd, kReject, 180, 397, 445, 100, PercentToSlider(state->directReject));
      ValueLabel(hwnd, kRejectValue, 642, 404);
      Label(hwnd, L"Sensitivity ratio", 30, 447, 105, 22);
      Edit(hwnd, kDirectThreshold, 137, 443, 72);
      Label(hwnd, L"(lower = catches more)", 214, 447, 130, 22, true);
      Label(hwnd, L"Recovery", 405, 447, 62, 22);
      Edit(hwnd, kDirectRecovery, 470, 443, 72);
      Label(hwnd, L"ms", 546, 447, 25, 22, true);

      Group(hwnd, L"Speaker voicing", 14, 502, 722, 142);
      Label(hwnd, L"Center sparkle", 30, 530, 150, 22);
      Slider(hwnd, kCenter, 180, 523, 445, 50, PercentToSlider(state->center));
      ValueLabel(hwnd, kCenterValue, 642, 530);

      Label(hwnd, L"Center band", 30, 572, 88, 22);
      Edit(hwnd, kCenterHp, 120, 568, 72);
      Label(hwnd, L"—", 197, 572, 15, 22);
      Edit(hwnd, kCenterLp, 215, 568, 76);
      Label(hwnd, L"Hz", 295, 572, 25, 22, true);

      Label(hwnd, L"Rear band", 348, 572, 72, 22);
      Edit(hwnd, kRearHp, 423, 568, 70);
      Label(hwnd, L"—", 497, 572, 15, 22);
      Edit(hwnd, kRearLp, 515, 568, 76);
      Label(hwnd, L"Hz", 595, 572, 25, 22, true);

      Label(hwnd, L"SL trim", 30, 609, 52, 22);
      Edit(hwnd, kRearLeftTrim, 84, 605, 65);
      Label(hwnd, L"SR trim", 178, 609, 52, 22);
      Edit(hwnd, kRearRightTrim, 232, 605, 65);
      Label(hwnd, L"1.00 = unity. Useful for room/speaker asymmetry.", 320, 609, 350, 20, true);

      Group(hwnd, L"Geometry / time alignment", 14, 652, 722, 78);
      Label(hwnd, L"FL", 30, 682, 24, 22); Edit(hwnd, kFl, 54, 678, 58);
      Label(hwnd, L"C", 133, 682, 18, 22); Edit(hwnd, kC, 151, 678, 58);
      Label(hwnd, L"FR", 229, 682, 24, 22); Edit(hwnd, kFr, 253, 678, 58);
      Label(hwnd, L"SL", 331, 682, 24, 22); Edit(hwnd, kSl, 355, 678, 58);
      Label(hwnd, L"SR", 433, 682, 24, 22); Edit(hwnd, kSr, 457, 678, 58);
      Label(hwnd, L"inches from listening position", 530, 682, 175, 22, true);

      HWND apply = Button(hwnd, kApply, L"APPLY LIVE", 20, 748, 150, 42);
      SendMessageW(apply, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
      Button(hwnd, kReload, L"RELOAD SAVED", 180, 748, 145, 42);

      HWND status = CreateWindowW(L"STATIC", L"Presets are non-destructive until Apply Live.",
                                  WS_CHILD | WS_VISIBLE,
                                  344, 758, 380, 30, hwnd,
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
                              id == kPresetAmbient ? L"Ambient" : L"v0.3 Baseline";
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
      CW_USEDEFAULT, CW_USEDEFAULT, 770, 845,
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
