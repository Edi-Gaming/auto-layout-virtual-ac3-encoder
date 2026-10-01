#include "StageVisual.h"

#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {

constexpr COLORREF kBg = RGB(15, 17, 23);
constexpr COLORREF kCard = RGB(20, 25, 34);
constexpr COLORREF kBorder = RGB(45, 56, 73);
constexpr COLORREF kText = RGB(238, 243, 251);
constexpr COLORREF kMuted = RGB(143, 158, 181);
constexpr COLORREF kGrid = RGB(40, 50, 65);
constexpr COLORREF kBlue = RGB(86, 180, 255);
constexpr COLORREF kGreen = RGB(86, 214, 154);
constexpr COLORREF kPurple = RGB(173, 126, 255);
constexpr COLORREF kAmber = RGB(241, 187, 84);
constexpr COLORREF kRose = RGB(243, 111, 142);

struct Vec2
{
  double x = 0.0;
  double y = 0.0;
};

struct StageState
{
  std::array<float, 5> distances{{33.0f, 30.0f, 33.0f, 27.0f, 33.0f}};
  OhlAnalyzerMetrics metrics{};
  bool metricsInitialized = false;
  std::array<POINT, 5> speakerPoints{};
  POINT listener{};
  int hoverSpeaker = -1;
  int dragSpeaker = -1;
  float scalePxPerInch = 3.0f;
};

constexpr std::array<Vec2, 5> kDirections{{
    {-0.72, -0.69},  // FL
    { 0.00, -1.00},  // C
    { 0.72, -0.69},  // FR
    {-0.82,  0.57},  // SL
    { 0.82,  0.57},  // SR
}};

constexpr const wchar_t* kNames[5] = {L"FL", L"C", L"FR", L"SL", L"SR"};

void FillRectColor(HDC dc, const RECT& r, COLORREF color)
{
  HBRUSH b = CreateSolidBrush(color);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

void FillRound(HDC dc, const RECT& r, int radius, COLORREF color)
{
  HBRUSH b = CreateSolidBrush(color);
  HPEN p = CreatePen(PS_NULL, 0, color);
  HGDIOBJ oldB = SelectObject(dc, b);
  HGDIOBJ oldP = SelectObject(dc, p);
  RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
  SelectObject(dc, oldP);
  SelectObject(dc, oldB);
  DeleteObject(p);
  DeleteObject(b);
}

void DrawTextSimple(HDC dc,
                    HFONT font,
                    COLORREF color,
                    const wchar_t* text,
                    RECT r,
                    UINT flags)
{
  HGDIOBJ oldF = font ? SelectObject(dc, font) : nullptr;
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  DrawTextW(dc, text, -1, &r, flags);
  if (oldF) SelectObject(dc, oldF);
}

COLORREF Blend(COLORREF a, COLORREF b, float t)
{
  t = std::clamp(t, 0.0f, 1.0f);
  const auto mix = [t](int x, int y) {
    return static_cast<int>(std::lround((1.0f - t) * x + t * y));
  };
  return RGB(
      mix(GetRValue(a), GetRValue(b)),
      mix(GetGValue(a), GetGValue(b)),
      mix(GetBValue(a), GetBValue(b)));
}

void DrawConnection(HDC dc, POINT a, POINT b, COLORREF color, int width)
{
  HPEN p = CreatePen(PS_SOLID, width, color);
  HGDIOBJ old = SelectObject(dc, p);
  MoveToEx(dc, a.x, a.y, nullptr);
  LineTo(dc, b.x, b.y);
  SelectObject(dc, old);
  DeleteObject(p);
}

float SpeakerActivity(const OhlAnalyzerMetrics& metrics, int stageIndex)
{
  static constexpr int channelMap[5] = {0, 2, 1, 4, 5};
  const float rms = std::max(0.0f, metrics.speakerRms[channelMap[stageIndex]]);
  return std::clamp(static_cast<float>(std::sqrt(rms * 4.5f)), 0.0f, 1.0f);
}

void ComputeGeometry(StageState& s, const RECT& r)
{
  const int w = static_cast<int>(r.right - r.left);
  const int h = static_cast<int>(r.bottom - r.top);
  s.listener = {w / 2, static_cast<LONG>(h * 57 / 100)};

  const float maxDistance = std::max(
      36.0f,
      *std::max_element(s.distances.begin(), s.distances.end()));

  const double availableFront = std::max(90.0, static_cast<double>(s.listener.y - 55));
  const double availableSide = std::max(90.0, static_cast<double>(w / 2 - 60));
  s.scalePxPerInch = static_cast<float>(std::min(
      3.4,
      std::min(availableFront, availableSide) / static_cast<double>(maxDistance)));

  for (int i = 0; i < 5; ++i)
  {
    const double radius = static_cast<double>(s.distances[i]) * s.scalePxPerInch;
    s.speakerPoints[i] = {
        s.listener.x + static_cast<LONG>(std::lround(kDirections[i].x * radius)),
        s.listener.y + static_cast<LONG>(std::lround(kDirections[i].y * radius))};
  }
}

int HitTestSpeaker(const StageState& s, int x, int y)
{
  for (int i = 0; i < 5; ++i)
  {
    const double dx = static_cast<double>(x - s.speakerPoints[i].x);
    const double dy = static_cast<double>(y - s.speakerPoints[i].y);
    if (dx * dx + dy * dy <= 28.0 * 28.0)
      return i;
  }
  return -1;
}

float DistanceFromMouse(const StageState& s, int speaker, int x, int y)
{
  const Vec2 dir = kDirections[static_cast<size_t>(speaker)];
  const double dx = static_cast<double>(x - s.listener.x);
  const double dy = static_cast<double>(y - s.listener.y);
  const double projection = dx * dir.x + dy * dir.y;
  const double inches = projection / std::max(0.1f, s.scalePxPerInch);
  return static_cast<float>(std::clamp(inches, 12.0, 120.0));
}

void SendDistanceChange(HWND hwnd, int speaker, float inches)
{
  const LPARAM packed = static_cast<LPARAM>(std::lround(inches * 100.0f));
  SendMessageW(
      GetParent(hwnd),
      OHL_STAGE_DISTANCE_CHANGED,
      static_cast<WPARAM>(speaker),
      packed);
}

void DrawSpeaker(HDC dc,
                 HFONT font,
                 POINT p,
                 const wchar_t* name,
                 float inches,
                 float activity,
                 bool hovered,
                 bool dragging,
                 COLORREF activeColor)
{
  activity = std::clamp(activity, 0.0f, 1.0f);
  const float interaction = dragging ? 1.0f : hovered ? 0.70f : 0.0f;
  const COLORREF shell = Blend(
      RGB(31, 39, 52),
      activeColor,
      std::clamp(0.18f + 0.55f * activity + 0.20f * interaction, 0.0f, 1.0f));

  const int halfW = hovered || dragging ? 23 : 20;
  const int halfH = hovered || dragging ? 27 : 24;
  RECT body{p.x - halfW, p.y - halfH, p.x + halfW, p.y + halfH - 4};

  if (activity > 0.05f)
  {
    const int glow = 5 + static_cast<int>(std::lround(activity * 9.0f));
    RECT glowRect{
        body.left - glow, body.top - glow,
        body.right + glow, body.bottom + glow};
    FillRound(dc, glowRect, 12, Blend(kCard, activeColor, 0.10f + 0.16f * activity));
  }

  FillRound(dc, body, 9, shell);

  HPEN border = CreatePen(
      PS_SOLID,
      dragging ? 2 : 1,
      Blend(kBorder, activeColor, std::max(activity, interaction)));
  HGDIOBJ oldP = SelectObject(dc, border);
  HGDIOBJ oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
  RoundRect(dc, body.left, body.top, body.right - 1, body.bottom - 1, 9, 9);
  SelectObject(dc, oldB);
  SelectObject(dc, oldP);
  DeleteObject(border);

  HBRUSH woofer = CreateSolidBrush(
      Blend(RGB(20, 26, 35), activeColor, 0.35f + 0.35f * activity));
  oldB = SelectObject(dc, woofer);
  oldP = SelectObject(dc, GetStockObject(NULL_PEN));
  Ellipse(dc, p.x - 9, p.y - 10, p.x + 9, p.y + 8);
  SelectObject(dc, oldP);
  SelectObject(dc, oldB);
  DeleteObject(woofer);

  RECT nameRect{p.x - 36, p.y + 24, p.x + 36, p.y + 42};
  DrawTextSimple(dc, font, kText, name, nameRect,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  wchar_t dist[32] = {};
  swprintf_s(dist, L"%.1f in", inches);
  RECT distRect{p.x - 40, p.y + 40, p.x + 40, p.y + 57};
  DrawTextSimple(
      dc, font,
      dragging || hovered ? activeColor : kMuted,
      dist, distRect,
      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawListener(HDC dc, HFONT font, POINT head, float field)
{
  const int halo = 34 + static_cast<int>(std::lround(25.0f * field));
  HPEN haloPen = CreatePen(PS_SOLID, 2, Blend(kGrid, kBlue, field));
  HGDIOBJ oldP = SelectObject(dc, haloPen);
  HGDIOBJ oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Ellipse(dc, head.x - halo, head.y - halo, head.x + halo, head.y + halo);
  SelectObject(dc, oldB);
  SelectObject(dc, oldP);
  DeleteObject(haloPen);

  HBRUSH ear = CreateSolidBrush(RGB(50, 60, 76));
  oldB = SelectObject(dc, ear);
  oldP = SelectObject(dc, GetStockObject(NULL_PEN));
  Ellipse(dc, head.x - 27, head.y - 10, head.x - 15, head.y + 10);
  Ellipse(dc, head.x + 15, head.y - 10, head.x + 27, head.y + 10);
  SelectObject(dc, oldP);
  SelectObject(dc, oldB);
  DeleteObject(ear);

  HBRUSH headBrush = CreateSolidBrush(RGB(45, 54, 69));
  oldB = SelectObject(dc, headBrush);
  oldP = SelectObject(dc, GetStockObject(NULL_PEN));
  Ellipse(dc, head.x - 21, head.y - 23, head.x + 21, head.y + 23);
  SelectObject(dc, oldP);
  SelectObject(dc, oldB);
  DeleteObject(headBrush);

  HPEN facing = CreatePen(PS_SOLID, 2, kText);
  oldP = SelectObject(dc, facing);
  MoveToEx(dc, head.x, head.y - 15, nullptr);
  LineTo(dc, head.x, head.y - 31);
  MoveToEx(dc, head.x - 6, head.y - 11, nullptr);
  LineTo(dc, head.x - 11, head.y - 19);
  MoveToEx(dc, head.x + 6, head.y - 11, nullptr);
  LineTo(dc, head.x + 11, head.y - 19);
  SelectObject(dc, oldP);
  DeleteObject(facing);

  RECT label{head.x - 48, head.y + 29, head.x + 48, head.y + 48};
  DrawTextSimple(dc, font, kMuted, L"LISTENER", label,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK StageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  auto* s = reinterpret_cast<StageState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE)
  {
    s = new StageState();
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
  }

  switch (msg)
  {
    case WM_SETCURSOR:
      if (s && (s->hoverSpeaker >= 0 || s->dragSpeaker >= 0))
      {
        SetCursor(LoadCursor(nullptr, s->dragSpeaker >= 0 ? IDC_SIZENS : IDC_HAND));
        return TRUE;
      }
      break;

    case WM_MOUSEMOVE:
      if (s)
      {
        RECT r{};
        GetClientRect(hwnd, &r);
        ComputeGeometry(*s, r);

        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);

        if (s->dragSpeaker >= 0)
        {
          const float inches = DistanceFromMouse(*s, s->dragSpeaker, x, y);
          s->distances[static_cast<size_t>(s->dragSpeaker)] = inches;
          SendDistanceChange(hwnd, s->dragSpeaker, inches);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        else
        {
          const int hit = HitTestSpeaker(*s, x, y);
          if (hit != s->hoverSpeaker)
          {
            s->hoverSpeaker = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
          TrackMouseEvent(&tme);
        }
      }
      return 0;

    case WM_MOUSELEAVE:
      if (s && s->dragSpeaker < 0 && s->hoverSpeaker >= 0)
      {
        s->hoverSpeaker = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_LBUTTONDOWN:
      if (s)
      {
        RECT r{};
        GetClientRect(hwnd, &r);
        ComputeGeometry(*s, r);
        const int hit = HitTestSpeaker(*s, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hit >= 0)
        {
          s->dragSpeaker = hit;
          s->hoverSpeaker = hit;
          SetCapture(hwnd);
          SetFocus(hwnd);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
      }
      return 0;

    case WM_LBUTTONUP:
      if (s && s->dragSpeaker >= 0)
      {
        const int speaker = s->dragSpeaker;
        const float inches = DistanceFromMouse(
            *s, speaker, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        s->distances[static_cast<size_t>(speaker)] = inches;
        SendDistanceChange(hwnd, speaker, inches);
        s->dragSpeaker = -1;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
    {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      RECT r{};
      GetClientRect(hwnd, &r);

      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bmp = CreateCompatibleBitmap(
          dc,
          std::max(1, static_cast<int>(r.right)),
          std::max(1, static_cast<int>(r.bottom)));
      HGDIOBJ oldBmp = SelectObject(mem, bmp);

      FillRectColor(mem, r, kBg);
      FillRound(mem, r, 14, kCard);

      HPEN border = CreatePen(PS_SOLID, 1, kBorder);
      HGDIOBJ oldP = SelectObject(mem, border);
      HGDIOBJ oldB = SelectObject(mem, GetStockObject(NULL_BRUSH));
      RoundRect(mem, r.left, r.top, r.right - 1, r.bottom - 1, 14, 14);
      SelectObject(mem, oldB);
      SelectObject(mem, oldP);
      DeleteObject(border);

      HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
      RECT title{16, 10, r.right - 16, 31};
      DrawTextSimple(mem, font, kText, L"LISTENING STAGE", title,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      RECT helper{16, 30, r.right - 16, 49};
      DrawTextSimple(mem, font, kMuted, L"drag a speaker radially to set its distance",
                     helper, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      if (s)
      {
        ComputeGeometry(*s, r);

        const float field = std::clamp(
            0.60f * s->metrics.ambience +
            0.40f * s->metrics.spatialBins,
            0.0f, 1.0f);

        const std::array<COLORREF, 5> colors{{
            kBlue, kPurple, kBlue, kGreen, kGreen}};

        for (int i = 0; i < 5; ++i)
        {
          const float activity = SpeakerActivity(s->metrics, i);
          DrawConnection(
              mem,
              s->listener,
              s->speakerPoints[i],
              Blend(kGrid, colors[static_cast<size_t>(i)], 0.18f + 0.72f * activity),
              activity > 0.55f ? 2 : 1);
        }

        DrawListener(mem, font, s->listener, field);

        for (int i = 0; i < 5; ++i)
        {
          DrawSpeaker(
              mem,
              font,
              s->speakerPoints[i],
              kNames[i],
              s->distances[static_cast<size_t>(i)],
              SpeakerActivity(s->metrics, i),
              s->hoverSpeaker == i,
              s->dragSpeaker == i,
              colors[static_cast<size_t>(i)]);
        }

        wchar_t scene[128] = {};
        swprintf_s(
            scene,
            L"field %d%%   rear %d%%   center %d%%",
            static_cast<int>(std::lround(field * 100.0f)),
            static_cast<int>(std::lround(std::clamp(s->metrics.rearOpen, 0.0f, 1.0f) * 100.0f)),
            static_cast<int>(std::lround(std::clamp(s->metrics.center, 0.0f, 1.0f) * 100.0f)));
        RECT fieldRect{16, r.bottom - 27, r.right - 16, r.bottom - 8};
        DrawTextSimple(
            mem,
            font,
            field > 0.30f ? kGreen : kMuted,
            scene,
            fieldRect,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
      }

      BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, oldBmp);
      DeleteObject(bmp);
      DeleteDC(mem);
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_NCDESTROY:
      delete s;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      break;
  }

  return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool RegisterOhlStageVisual(HINSTANCE instance)
{
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = StageProc;
  wc.hInstance = instance;
  wc.lpszClassName = kOhlStageClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND CreateOhlStageVisual(HWND parent, int id, int x, int y, int w, int h)
{
  return CreateWindowW(
      kOhlStageClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      x, y, w, h, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
}

void UpdateOhlStageVisual(HWND hwnd,
                          const std::array<float, 5>& distancesInches,
                          const OhlAnalyzerMetrics& metrics)
{
  if (!hwnd)
    return;

  auto* s = reinterpret_cast<StageState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!s)
    return;

  s->distances = distancesInches;

  if (!s->metricsInitialized)
  {
    s->metrics = metrics;
    s->metricsInitialized = true;
  }
  else
  {
    const auto smooth = [](float current, float target, float attack, float release) {
      const float alpha = target > current ? attack : release;
      return current + alpha * (target - current);
    };

    s->metrics.online = metrics.online;
    s->metrics.sequence = metrics.sequence;
    s->metrics.ambience = smooth(s->metrics.ambience, metrics.ambience, 0.55f, 0.22f);
    s->metrics.center = smooth(s->metrics.center, metrics.center, 0.55f, 0.22f);
    s->metrics.spatialBins = smooth(s->metrics.spatialBins, metrics.spatialBins, 0.55f, 0.22f);
    s->metrics.rearOpen = smooth(s->metrics.rearOpen, metrics.rearOpen, 0.55f, 0.20f);

    for (size_t i = 0; i < s->metrics.speakerRms.size(); ++i)
      s->metrics.speakerRms[i] =
          smooth(s->metrics.speakerRms[i], metrics.speakerRms[i], 0.65f, 0.20f);
  }

  InvalidateRect(hwnd, nullptr, FALSE);
}
