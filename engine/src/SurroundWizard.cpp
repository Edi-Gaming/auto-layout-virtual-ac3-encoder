#include "SurroundWizard.h"

#include "BrandIcon.h"
#include "ModeControl.h"
#include "ModernControls.h"

#include <commctrl.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <sstream>
#include <string>

namespace {

constexpr wchar_t kClassName[] = L"OhlSurroundWizardWindow";

enum ControlId
{
  kFL = 4101,
  kC,
  kFR,
  kSL,
  kLFE,
  kSR,
  kFLLFE,
  kStop,
  kFreq,
  kLevel,
  kFreqLabel,
  kLevelLabel,
  kStatus,
  kStep,
  kQ40,
  kQ60,
  kQ80,
  kQ100,
  kQ120,
  kQ160,
  kQ200,
  kQ500,
  kQ1000,
  kChannelWalk,
  kBassMatrix,
  kFlLfeAb,
  kFlLfeM10,
  kFlLfeInv
};

constexpr UINT_PTR kSweepTimer = 23;
constexpr UINT_PTR kAutoTimer = 24;
constexpr int kSweepMs = 1500;
constexpr int kAutoMs = 1300;
constexpr std::array<int, 7> kSweepFull{{40, 60, 80, 100, 120, 160, 200}};
constexpr std::array<int, 5> kSweepLfe{{40, 60, 80, 100, 120}};

HBRUSH gBg = nullptr;
HFONT gTitle = nullptr;
HFONT gUi = nullptr;
HFONT gSmall = nullptr;
std::string gRoute;
bool gSweep = false;
size_t gSweepIndex = 0;

enum class AutoMode
{
  None,
  ChannelWalk,
  BassMatrix,
  FlLfeAb
};

AutoMode gAutoMode = AutoMode::None;
size_t gAutoIndex = 0;
constexpr std::array<const char*, 5> kChannelWalkRoutes{{"fl", "c", "fr", "sr", "sl"}};
constexpr std::array<const char*, 5> kBassMatrixRoutes{{"fl", "c", "fr", "sl", "sr"}};

std::map<std::string, std::string> ParseFields(const std::string& text)
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

int Freq(HWND hwnd)
{
  return static_cast<int>(SendDlgItemMessageW(hwnd, kFreq, TBM_GETPOS, 0, 0));
}

double LevelDb(HWND hwnd)
{
  const int raw = static_cast<int>(SendDlgItemMessageW(hwnd, kLevel, TBM_GETPOS, 0, 0));
  return -42.0 + static_cast<double>(raw);
}

bool IsLfeRoute(const std::string& route)
{
  return route == "lfe" || route == "fl+lfe" ||
         route == "fl+lfe-10" || route == "fl+lfe-inv";
}

void UpdateValueLabels(HWND hwnd)
{
  const int hz = Freq(hwnd);
  const double db = LevelDb(hwnd);

  std::wostringstream f;
  f << hz << L" Hz";
  SetDlgItemTextW(hwnd, kFreqLabel, f.str().c_str());

  std::wostringstream l;
  l.setf(std::ios::fixed);
  l.precision(0);
  l << db << L" dBFS";
  SetDlgItemTextW(hwnd, kLevelLabel, l.str().c_str());
}

void CheckRouteButtons(HWND hwnd, const std::string& route)
{
  const std::array<std::pair<int, const char*>, 7> routes{{
      {kFL, "fl"}, {kC, "c"}, {kFR, "fr"}, {kSL, "sl"},
      {kLFE, "lfe"}, {kSR, "sr"}, {kFLLFE, "fl+lfe"}
  }};
  for (const auto& item : routes)
    SendDlgItemMessageW(
        hwnd, item.first, BM_SETCHECK,
        route == item.second ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SetStatus(HWND hwnd, const std::wstring& text)
{
  SetDlgItemTextW(hwnd, kStatus, text.c_str());
}

void StopSweep(HWND hwnd)
{
  if (gSweep)
  {
    KillTimer(hwnd, kSweepTimer);
    gSweep = false;
    gSweepIndex = 0;
    SetWindowTextW(GetDlgItem(hwnd, kStep), L"STEP 40 → 200");
  }
}

void StopAuto(HWND hwnd)
{
  if (gAutoMode != AutoMode::None)
  {
    KillTimer(hwnd, kAutoTimer);
    gAutoMode = AutoMode::None;
    gAutoIndex = 0;
  }
}

void StopTest(HWND hwnd, bool updateUi = true)
{
  StopSweep(hwnd);
  StopAuto(hwnd);
  std::string response;
  SendModeCommand("test stop", response, 500);
  gRoute.clear();
  CheckRouteButtons(hwnd, "");
  if (updateUi)
    SetStatus(hwnd, L"READY — normal OHL Music restored");
}

bool StartRoute(HWND hwnd, std::string route, bool fromSweep = false)
{
  int hz = Freq(hwnd);
  if (IsLfeRoute(route) && hz > 120)
  {
    hz = 120;
    SendDlgItemMessageW(hwnd, kFreq, TBM_SETPOS, TRUE, hz);
    UpdateValueLabels(hwnd);
  }

  const double db = LevelDb(hwnd);
  std::ostringstream cmd;
  cmd.setf(std::ios::fixed);
  cmd.precision(1);
  cmd << "test tone " << route << " " << hz << " " << db;

  std::string response;
  if (!SendModeCommand(cmd.str(), response, 800))
  {
    SetStatus(hwnd, L"ENGINE OFFLINE — test not started");
    return false;
  }

  const auto fields = ParseFields(response);
  auto state = fields.find("test_state");
  if (state == fields.end() || state->second != "active")
  {
    std::wstring msg = L"TEST ERROR";
    auto err = fields.find("error");
    if (err != fields.end())
      msg += L" — " + std::wstring(err->second.begin(), err->second.end());
    SetStatus(hwnd, msg);
    return false;
  }

  if (!fromSweep)
  {
    StopSweep(hwnd);
    StopAuto(hwnd);
  }

  gRoute = route;
  CheckRouteButtons(hwnd, route);

  std::wostringstream status;
  status << L"PLAYING  " << std::wstring(route.begin(), route.end())
         << L"  ·  " << hz << L" Hz  ·  ";
  status.setf(std::ios::fixed);
  status.precision(0);
  status << db << L" dBFS";
  if (IsLfeRoute(route))
    status << L"  ·  Dolby LFE limited to ≤120 Hz";
  SetStatus(hwnd, status.str());
  return true;
}

void SetQuickFrequency(HWND hwnd, int hz)
{
  SendDlgItemMessageW(hwnd, kFreq, TBM_SETPOS, TRUE, hz);
  UpdateValueLabels(hwnd);
  if (!gRoute.empty())
    StartRoute(hwnd, gRoute);
}

void StartSweep(HWND hwnd)
{
  if (gRoute.empty())
    gRoute = "fl";

  gSweep = true;
  gSweepIndex = 0;
  SetWindowTextW(GetDlgItem(hwnd, kStep), L"STOP STEP TEST");

  const int hz = IsLfeRoute(gRoute) ? kSweepLfe[0] : kSweepFull[0];
  SendDlgItemMessageW(hwnd, kFreq, TBM_SETPOS, TRUE, hz);
  UpdateValueLabels(hwnd);
  StartRoute(hwnd, gRoute, true);
  SetTimer(hwnd, kSweepTimer, kSweepMs, nullptr);
}

void AdvanceSweep(HWND hwnd)
{
  if (!gSweep || gRoute.empty())
    return;

  if (IsLfeRoute(gRoute))
  {
    gSweepIndex = (gSweepIndex + 1) % kSweepLfe.size();
    SendDlgItemMessageW(hwnd, kFreq, TBM_SETPOS, TRUE, kSweepLfe[gSweepIndex]);
  }
  else
  {
    gSweepIndex = (gSweepIndex + 1) % kSweepFull.size();
    SendDlgItemMessageW(hwnd, kFreq, TBM_SETPOS, TRUE, kSweepFull[gSweepIndex]);
  }

  UpdateValueLabels(hwnd);
  StartRoute(hwnd, gRoute, true);
}

void StartAutoMode(HWND hwnd, AutoMode mode)
{
  StopSweep(hwnd);
  StopAuto(hwnd);
  gAutoMode = mode;
  gAutoIndex = 0;

  if (mode == AutoMode::ChannelWalk)
  {
    SendDlgItemMessageW(hwnd, kFreq, TBM_SETPOS, TRUE, 1000);
    UpdateValueLabels(hwnd);
    StartRoute(hwnd, kChannelWalkRoutes[0], true);
    SetStatus(hwnd, L"CHANNEL WALK — FL → C → FR → SR → SL @ 1 kHz");
  }
  else if (mode == AutoMode::BassMatrix)
  {
    int hz = Freq(hwnd);
    if (hz > 200)
    {
      hz = 80;
      SendDlgItemMessageW(hwnd, kFreq, TBM_SETPOS, TRUE, hz);
      UpdateValueLabels(hwnd);
    }
    StartRoute(hwnd, kBassMatrixRoutes[0], true);
    SetStatus(hwnd, L"BASS MATRIX — FL → C → FR → SL → SR");
  }
  else if (mode == AutoMode::FlLfeAb)
  {
    int hz = Freq(hwnd);
    if (hz > 120)
    {
      hz = 80;
      SendDlgItemMessageW(hwnd, kFreq, TBM_SETPOS, TRUE, hz);
      UpdateValueLabels(hwnd);
    }
    StartRoute(hwnd, "fl", true);
    SetStatus(hwnd, L"A/B — FL redirected bass ↔ discrete LFE");
  }

  SetTimer(hwnd, kAutoTimer, kAutoMs, nullptr);
}

void AdvanceAuto(HWND hwnd)
{
  switch (gAutoMode)
  {
    case AutoMode::ChannelWalk:
      gAutoIndex = (gAutoIndex + 1) % kChannelWalkRoutes.size();
      StartRoute(hwnd, kChannelWalkRoutes[gAutoIndex], true);
      break;

    case AutoMode::BassMatrix:
      gAutoIndex = (gAutoIndex + 1) % kBassMatrixRoutes.size();
      StartRoute(hwnd, kBassMatrixRoutes[gAutoIndex], true);
      break;

    case AutoMode::FlLfeAb:
      gAutoIndex = (gAutoIndex + 1) % 2;
      StartRoute(hwnd, gAutoIndex == 0 ? "fl" : "lfe", true);
      break;

    case AutoMode::None:
      break;
  }
}

HWND Label(HWND parent, const wchar_t* text, int x, int y, int w, int h, bool useSmall = false)
{
  const bool onCard =
      (x >= 475 && x < 785 && y >= 104 && y < 485) ||
      (x >= 20 && x < 785 && y >= 500 && y < 612);
  HWND c = CreateWindowW(
      onCard ? kOhlCardLabelClass : L"STATIC", text, WS_CHILD | WS_VISIBLE,
      x, y, w, h, parent, nullptr, nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(useSmall ? gSmall : gUi), TRUE);
  return c;
}

HWND Button(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h = 34)
{
  HWND c = CreateWindowW(
      kOhlButtonClass, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      x, y, w, h, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(gUi), TRUE);
  return c;
}

void DrawRoom(HDC dc)
{
  RECT room{20, 104, 455, 485};
  HBRUSH panel = CreateSolidBrush(RGB(12, 15, 21));
  FillRect(dc, &room, panel);
  DeleteObject(panel);

  HPEN border = CreatePen(PS_SOLID, 1, RGB(47, 56, 69));
  HGDIOBJ oldPen = SelectObject(dc, border);
  HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  RoundRect(dc, room.left, room.top, room.right, room.bottom, 18, 18);

  const POINT center{237, 292};
  for (int radius : {55, 108, 162})
    Ellipse(dc, center.x - radius, center.y - radius, center.x + radius, center.y + radius);

  HPEN ray = CreatePen(PS_DOT, 1, RGB(38, 47, 60));
  SelectObject(dc, ray);
  MoveToEx(dc, center.x, 145, nullptr); LineTo(dc, center.x, 440);
  MoveToEx(dc, 65, center.y, nullptr); LineTo(dc, 410, center.y);

  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(ray);
  DeleteObject(border);

  HBRUSH listener = CreateSolidBrush(RGB(9, 12, 17));
  HPEN listenerPen = CreatePen(PS_SOLID, 1, RGB(126, 140, 163));
  oldPen = SelectObject(dc, listenerPen);
  oldBrush = SelectObject(dc, listener);
  Ellipse(dc, center.x - 27, center.y - 27, center.x + 27, center.y + 27);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(listener);
  DeleteObject(listenerPen);

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(189, 201, 218));
  HGDIOBJ oldFont = SelectObject(dc, gSmall);
  RECT you{center.x - 30, center.y - 8, center.x + 30, center.y + 10};
  DrawTextW(dc, L"YOU", -1, &you, DT_CENTER | DT_SINGLELINE);
  SelectObject(dc, oldFont);

  SetTextColor(dc, RGB(139, 151, 169));
  RECT front{177, 114, 297, 134};
  DrawTextW(dc, L"FRONT", -1, &front, DT_CENTER | DT_SINGLELINE);
}

LRESULT CALLBACK WizardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_CREATE:
    {
      gTitle = CreateFontW(-29, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      gUi = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      gSmall = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

      HWND title = Label(hwnd, L"OHL  |  SURROUND WIZARD", 20, 15, 520, 36);
      SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(gTitle), TRUE);
      Label(hwnd, L"STR-K900 bass-routing probe  ·  discrete AC-3 5.1 diagnostics",
            21, 50, 550, 21, true);

      HWND status = CreateWindowW(
          kOhlCardLabelClass, L"READY — select a speaker",
          WS_CHILD | WS_VISIBLE,
          505, 19, 280, 48, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatus)), nullptr, nullptr);
      SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(gSmall), TRUE);

      // Speaker nodes over the painted field, matching the old Surround Lab geometry.
      Button(hwnd, kFL,  L"FL",  70, 150, 78, 38);
      Button(hwnd, kC,   L"C",  198, 132, 78, 38);
      Button(hwnd, kFR,  L"FR", 326, 150, 78, 38);
      Button(hwnd, kSL,  L"SL",  54, 372, 78, 38);
      Button(hwnd, kLFE, L"LFE",198, 420, 78, 38);
      Button(hwnd, kSR,  L"SR", 342, 372, 78, 38);

      HWND group = CreateWindowW(
          kOhlGroupClass, L"Signal / bass management",
          WS_CHILD | WS_VISIBLE,
          475, 104, 310, 381, hwnd, nullptr, nullptr, nullptr);
      SendMessageW(group, WM_SETFONT, reinterpret_cast<WPARAM>(gUi), TRUE);

      Label(hwnd, L"Frequency", 495, 142, 100, 22);
      HWND fl = Label(hwnd, L"80 Hz", 688, 142, 72, 22);
      SetWindowLongPtrW(fl, GWLP_ID, kFreqLabel);
      SetWindowTextW(fl, L"80 Hz");

      HWND freq = CreateWindowW(
          kOhlSliderClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          493, 166, 270, 32, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFreq)), nullptr, nullptr);
      SendMessageW(freq, TBM_SETRANGE, TRUE, MAKELPARAM(20, 2000));
      SendMessageW(freq, TBM_SETPOS, TRUE, 80);

      Label(hwnd, L"Output level", 495, 207, 110, 22);
      HWND ll = Label(hwnd, L"-30 dBFS", 675, 207, 88, 22);
      SetWindowLongPtrW(ll, GWLP_ID, kLevelLabel);

      HWND level = CreateWindowW(
          kOhlSliderClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          493, 231, 270, 32, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLevel)), nullptr, nullptr);
      SendMessageW(level, TBM_SETRANGE, TRUE, MAKELPARAM(0, 36));
      SendMessageW(level, TBM_SETPOS, TRUE, 12); // -30 dBFS

      Label(hwnd, L"QUICK Hz", 495, 278, 72, 20, true);
      Button(hwnd, kQ40,  L"40",  495, 302, 50, 31);
      Button(hwnd, kQ60,  L"60",  551, 302, 50, 31);
      Button(hwnd, kQ80,  L"80",  607, 302, 50, 31);
      Button(hwnd, kQ100, L"100", 663, 302, 50, 31);
      Button(hwnd, kQ120, L"120", 719, 302, 50, 31);
      Button(hwnd, kQ160, L"160", 495, 339, 58, 31);
      Button(hwnd, kQ200, L"200", 561, 339, 58, 31);
      Button(hwnd, kQ500, L"500", 627, 339, 58, 31);
      Button(hwnd, kQ1000, L"1 kHz", 693, 339, 76, 31);

      Button(hwnd, kFLLFE, L"FL + LFE", 495, 382, 120, 31);
      Button(hwnd, kStep, L"BASS STEP 40 → 200", 623, 382, 146, 31);
      Button(hwnd, kStop, L"STOP ALL", 668, 421, 101, 34);

      Label(hwnd,
            L"500 Hz / 1 kHz = channel ID.  40–200 Hz = bass routing.  LFE is capped at 120 Hz.",
            495, 421, 165, 48, true);

      HWND characterize = CreateWindowW(
          kOhlGroupClass, L"Characterize",
          WS_CHILD | WS_VISIBLE,
          20, 500, 765, 112, hwnd, nullptr, nullptr, nullptr);
      SendMessageW(characterize, WM_SETFONT, reinterpret_cast<WPARAM>(gUi), TRUE);

      Button(hwnd, kChannelWalk, L"CHANNEL WALK 1 kHz", 38, 535, 150, 34);
      Button(hwnd, kBassMatrix, L"BASS MATRIX", 198, 535, 125, 34);
      Button(hwnd, kFlLfeAb, L"FL ↔ LFE A/B", 333, 535, 125, 34);
      Button(hwnd, kFlLfeM10, L"FL + LFE -10 dB", 468, 535, 135, 34);
      Button(hwnd, kFlLfeInv, L"FL + LFE 180°", 613, 535, 140, 34);

      Label(hwnd,
            L"Automated walks use conservative level.  -10 dB checks LFE calibration; 180° checks crossover phase interaction.",
            38, 578, 715, 24, true);

      Label(hwnd,
            L"Closing this window stops all diagnostics and restores normal OHL Music.",
            20, 625, 760, 24, true);

      UpdateValueLabels(hwnd);

      std::string initial;
      if (SendModeCommand("test status", initial, 300))
      {
        const auto f = ParseFields(initial);
        auto st = f.find("test_state");
        if (st != f.end() && st->second == "active")
        {
          auto route = f.find("route");
          auto hz = f.find("frequency_hz");
          auto db = f.find("level_db");
          if (route != f.end())
            gRoute = route->second;
          if (hz != f.end())
            SendDlgItemMessageW(hwnd, kFreq, TBM_SETPOS, TRUE, std::stoi(hz->second));
          if (db != f.end())
          {
            const int pos = std::clamp(
                static_cast<int>(std::lround(std::stod(db->second) + 42.0)), 0, 36);
            SendDlgItemMessageW(hwnd, kLevel, TBM_SETPOS, TRUE, pos);
          }
          UpdateValueLabels(hwnd);
          CheckRouteButtons(hwnd, gRoute);
          StartRoute(hwnd, gRoute);
        }
      }
      else
      {
        SetStatus(hwnd, L"ENGINE OFFLINE — start Surround mode first");
      }

      return 0;
    }

    case WM_HSCROLL:
    {
      UpdateValueLabels(hwnd);
      const int code = LOWORD(wp);
      if (!gRoute.empty() && (code == TB_ENDTRACK || code == TB_THUMBTRACK))
        StartRoute(hwnd, gRoute);
      return 0;
    }

    case WM_COMMAND:
    {
      const int id = LOWORD(wp);
      switch (id)
      {
        case kFL:  StartRoute(hwnd, "fl"); return 0;
        case kC:   StartRoute(hwnd, "c"); return 0;
        case kFR:  StartRoute(hwnd, "fr"); return 0;
        case kSL:  StartRoute(hwnd, "sl"); return 0;
        case kLFE: StartRoute(hwnd, "lfe"); return 0;
        case kSR:  StartRoute(hwnd, "sr"); return 0;
        case kFLLFE: StartRoute(hwnd, "fl+lfe"); return 0;
        case kStop: StopTest(hwnd); return 0;
        case kStep:
          if (gSweep) { StopSweep(hwnd); SetStatus(hwnd, L"STEP TEST PAUSED"); }
          else StartSweep(hwnd);
          return 0;
        case kQ40:  SetQuickFrequency(hwnd, 40); return 0;
        case kQ60:  SetQuickFrequency(hwnd, 60); return 0;
        case kQ80:  SetQuickFrequency(hwnd, 80); return 0;
        case kQ100: SetQuickFrequency(hwnd, 100); return 0;
        case kQ120: SetQuickFrequency(hwnd, 120); return 0;
        case kQ160: SetQuickFrequency(hwnd, 160); return 0;
        case kQ200: SetQuickFrequency(hwnd, 200); return 0;
        case kQ500: SetQuickFrequency(hwnd, 500); return 0;
        case kQ1000: SetQuickFrequency(hwnd, 1000); return 0;
        case kChannelWalk: StartAutoMode(hwnd, AutoMode::ChannelWalk); return 0;
        case kBassMatrix: StartAutoMode(hwnd, AutoMode::BassMatrix); return 0;
        case kFlLfeAb: StartAutoMode(hwnd, AutoMode::FlLfeAb); return 0;
        case kFlLfeM10: StartRoute(hwnd, "fl+lfe-10"); return 0;
        case kFlLfeInv: StartRoute(hwnd, "fl+lfe-inv"); return 0;
      }
      break;
    }

    case WM_TIMER:
      if (wp == kSweepTimer)
      {
        AdvanceSweep(hwnd);
        return 0;
      }
      if (wp == kAutoTimer)
      {
        AdvanceAuto(hwnd);
        return 0;
      }
      break;

    case WM_PAINT:
    {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      DrawRoom(dc);
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
      HDC dc = reinterpret_cast<HDC>(wp);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(226, 232, 242));
      return reinterpret_cast<LRESULT>(gBg);
    }

    case WM_CLOSE:
      StopTest(hwnd, false);
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      StopSweep(hwnd);
      StopAuto(hwnd);
      if (gTitle) { DeleteObject(gTitle); gTitle = nullptr; }
      if (gUi) { DeleteObject(gUi); gUi = nullptr; }
      if (gSmall) { DeleteObject(gSmall); gSmall = nullptr; }
      if (gBg) { DeleteObject(gBg); gBg = nullptr; }
      PostQuitMessage(0);
      return 0;
  }

  return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int RunSurroundWizardGui()
{
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_BAR_CLASSES;
  InitCommonControlsEx(&icc);

  HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!RegisterOhlModernControls(instance))
    return 1;

  gBg = CreateSolidBrush(RGB(7, 8, 11));

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WizardProc;
  wc.hInstance = instance;
  wc.lpszClassName = kClassName;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = GetOhlBrandIcon();
  wc.hIconSm = GetOhlBrandIcon();
  wc.hbrBackground = gBg;
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(
      0, kClassName, L"OHL Surround Wizard",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 820, 700,
      nullptr, nullptr, instance, nullptr);
  if (!hwnd)
    return 1;

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}
