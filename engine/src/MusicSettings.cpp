#include "MusicSettings.h"
#include "BrandIcon.h"
#include "ModeControl.h"

#include <commctrl.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kClassName[] = L"OhlMusicSettingsWindow";

constexpr int kEnable = 3001;
constexpr int kAmbience = 3002;
constexpr int kWidth = 3003;
constexpr int kCenter = 3004;
constexpr int kAmbienceValue = 3005;
constexpr int kWidthValue = 3006;
constexpr int kCenterValue = 3007;
constexpr int kCenterHz = 3008;
constexpr int kRearHz = 3009;
constexpr int kFl = 3010;
constexpr int kC = 3011;
constexpr int kFr = 3012;
constexpr int kSl = 3013;
constexpr int kSr = 3014;
constexpr int kApply = 3015;
constexpr int kStatus = 3016;

HBRUSH gBg = nullptr;
HFONT gTitleFont = nullptr;
HFONT gUiFont = nullptr;

struct SettingsState
{
  std::string configPath;
  bool enabled = true;
  double ambience = 0.78;
  double width = 0.22;
  double center = 0.18;
  double centerHz = 2400.0;
  double rearHz = 140.0;
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
    std::string s = Trim(line);
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
  s.center = ReadDouble(v, "music_center_treble_gain", s.center);
  s.centerHz = ReadDouble(v, "music_center_treble_hz", s.centerHz);
  s.rearHz = ReadDouble(v, "music_rear_highpass_hz", s.rearHz);
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

void UpdateSliderLabels(HWND hwnd)
{
  const int a = static_cast<int>(SendDlgItemMessageW(hwnd, kAmbience, TBM_GETPOS, 0, 0));
  const int w = static_cast<int>(SendDlgItemMessageW(hwnd, kWidth, TBM_GETPOS, 0, 0));
  const int c = static_cast<int>(SendDlgItemMessageW(hwnd, kCenter, TBM_GETPOS, 0, 0));

  const std::wstring av = std::to_wstring(a) + L"%";
  const std::wstring wv = std::to_wstring(w) + L"%";
  const std::wstring cv = std::to_wstring(c) + L"%";
  SetDlgItemTextW(hwnd, kAmbienceValue, av.c_str());
  SetDlgItemTextW(hwnd, kWidthValue, wv.c_str());
  SetDlgItemTextW(hwnd, kCenterValue, cv.c_str());
}

void Apply(HWND hwnd, SettingsState& state)
{
  state.enabled = IsDlgButtonChecked(hwnd, kEnable) == BST_CHECKED;
  state.ambience =
      static_cast<double>(SendDlgItemMessageW(hwnd, kAmbience, TBM_GETPOS, 0, 0)) / 100.0;
  state.width =
      static_cast<double>(SendDlgItemMessageW(hwnd, kWidth, TBM_GETPOS, 0, 0)) / 100.0;
  state.center =
      static_cast<double>(SendDlgItemMessageW(hwnd, kCenter, TBM_GETPOS, 0, 0)) / 100.0;

  state.centerHz = std::clamp(GetDoubleEdit(hwnd, kCenterHz, state.centerHz), 200.0, 12000.0);
  state.rearHz = std::clamp(GetDoubleEdit(hwnd, kRearHz, state.rearHz), 0.0, 1000.0);
  state.fl = std::clamp(GetDoubleEdit(hwnd, kFl, state.fl), 0.0, 300.0);
  state.c = std::clamp(GetDoubleEdit(hwnd, kC, state.c), 0.0, 300.0);
  state.fr = std::clamp(GetDoubleEdit(hwnd, kFr, state.fr), 0.0, 300.0);
  state.sl = std::clamp(GetDoubleEdit(hwnd, kSl, state.sl), 0.0, 300.0);
  state.sr = std::clamp(GetDoubleEdit(hwnd, kSr, state.sr), 0.0, 300.0);

  std::map<std::string, std::string> values;
  values["stereo_processing"] = state.enabled ? "music" : "receiver";
  values["music_surround_gain"] = Fmt(state.ambience);
  values["music_width_floor"] = Fmt(state.width);
  values["music_center_treble_gain"] = Fmt(state.center);
  values["music_center_treble_hz"] = Fmt(state.centerHz, 0);
  values["music_rear_highpass_hz"] = Fmt(state.rearHz, 0);
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

HWND Label(HWND parent, const wchar_t* text, int x, int y, int w, int h)
{
  HWND c = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                         x, y, w, h, parent, nullptr, nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  return c;
}

HWND Edit(HWND parent, int id, int x, int y, int w)
{
  HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                           x, y, w, 25, parent, reinterpret_cast<HMENU>(id), nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
  return c;
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

      gTitleFont = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      gUiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

      HWND title = CreateWindowW(L"STATIC", L"OHL  |  MUSIC SPATIAL",
                                 WS_CHILD | WS_VISIBLE,
                                 22, 16, 560, 34, hwnd, nullptr, nullptr, nullptr);
      SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(gTitleFont), TRUE);
      Label(hwnd, L"Stereo -> OHL spatial 5.1. Native multichannel still bypasses this DSP.",
            23, 50, 600, 24);

      HWND enable = CreateWindowW(L"BUTTON", L"Enable OHL Music for stereo",
                                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                  22, 82, 300, 26,
                                  hwnd, reinterpret_cast<HMENU>(kEnable), nullptr, nullptr);
      SendMessageW(enable, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
      CheckDlgButton(hwnd, kEnable, state->enabled ? BST_CHECKED : BST_UNCHECKED);

      Label(hwnd, L"Adaptive ambience", 22, 122, 160, 23);
      HWND amb = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
                               175, 116, 340, 34, hwnd,
                               reinterpret_cast<HMENU>(kAmbience), nullptr, nullptr);
      SendMessageW(amb, TBM_SETRANGE, TRUE, MAKELPARAM(0, 120));
      SendMessageW(amb, TBM_SETPOS, TRUE,
                   static_cast<LPARAM>(std::lround(state->ambience * 100.0)));
      Label(hwnd, L"Always-on width", 22, 162, 160, 23);
      HWND width = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
                                 175, 156, 340, 34, hwnd,
                                 reinterpret_cast<HMENU>(kWidth), nullptr, nullptr);
      SendMessageW(width, TBM_SETRANGE, TRUE, MAKELPARAM(0, 60));
      SendMessageW(width, TBM_SETPOS, TRUE,
                   static_cast<LPARAM>(std::lround(state->width * 100.0)));

      Label(hwnd, L"Center sparkle", 22, 202, 160, 23);
      HWND center = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
                                  175, 196, 340, 34, hwnd,
                                  reinterpret_cast<HMENU>(kCenter), nullptr, nullptr);
      SendMessageW(center, TBM_SETRANGE, TRUE, MAKELPARAM(0, 50));
      SendMessageW(center, TBM_SETPOS, TRUE,
                   static_cast<LPARAM>(std::lround(state->center * 100.0)));

      HWND av = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                              525, 122, 65, 23, hwnd,
                              reinterpret_cast<HMENU>(kAmbienceValue), nullptr, nullptr);
      HWND wv = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                              525, 162, 65, 23, hwnd,
                              reinterpret_cast<HMENU>(kWidthValue), nullptr, nullptr);
      HWND cv = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                              525, 202, 65, 23, hwnd,
                              reinterpret_cast<HMENU>(kCenterValue), nullptr, nullptr);
      SendMessageW(av, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
      SendMessageW(wv, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
      SendMessageW(cv, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);

      Label(hwnd, L"Center HP", 22, 246, 90, 23);
      Edit(hwnd, kCenterHz, 105, 242, 75);
      Label(hwnd, L"Hz", 185, 246, 28, 23);
      SetDoubleEdit(hwnd, kCenterHz, state->centerHz, 0);

      Label(hwnd, L"Rear HP", 245, 246, 75, 23);
      Edit(hwnd, kRearHz, 315, 242, 75);
      Label(hwnd, L"Hz", 395, 246, 28, 23);
      SetDoubleEdit(hwnd, kRearHz, state->rearHz, 0);

      Label(hwnd, L"Speaker distances from listening position (inches)", 22, 292, 420, 23);
      Label(hwnd, L"FL", 22, 326, 25, 23); Edit(hwnd, kFl, 47, 321, 58);
      Label(hwnd, L"C", 124, 326, 20, 23); Edit(hwnd, kC, 145, 321, 58);
      Label(hwnd, L"FR", 222, 326, 25, 23); Edit(hwnd, kFr, 248, 321, 58);
      Label(hwnd, L"SL", 326, 326, 25, 23); Edit(hwnd, kSl, 351, 321, 58);
      Label(hwnd, L"SR", 430, 326, 25, 23); Edit(hwnd, kSr, 455, 321, 58);

      SetDoubleEdit(hwnd, kFl, state->fl);
      SetDoubleEdit(hwnd, kC, state->c);
      SetDoubleEdit(hwnd, kFr, state->fr);
      SetDoubleEdit(hwnd, kSl, state->sl);
      SetDoubleEdit(hwnd, kSr, state->sr);

      HWND apply = CreateWindowW(L"BUTTON", L"APPLY LIVE",
                                 WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                 22, 380, 155, 42, hwnd,
                                 reinterpret_cast<HMENU>(kApply), nullptr, nullptr);
      SendMessageW(apply, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);

      HWND status = CreateWindowW(L"STATIC", L"Changes are persisted to the installed OHL config.",
                                  WS_CHILD | WS_VISIBLE,
                                  195, 390, 395, 30, hwnd,
                                  reinterpret_cast<HMENU>(kStatus), nullptr, nullptr);
      SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);

      UpdateSliderLabels(hwnd);
      return 0;
    }

    case WM_HSCROLL:
      UpdateSliderLabels(hwnd);
      return 0;

    case WM_COMMAND:
      if (LOWORD(wp) == kApply && state)
      {
        Apply(hwnd, *state);
        return 0;
      }
      break;

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
      0, kClassName, L"OHL Music Settings",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 630, 480,
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
