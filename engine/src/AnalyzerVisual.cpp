#include "AnalyzerVisual.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <string>

namespace {

constexpr COLORREF kBg = RGB(15, 17, 23);
constexpr COLORREF kCard = RGB(20, 25, 34);
constexpr COLORREF kCard2 = RGB(24, 30, 40);
constexpr COLORREF kBorder = RGB(45, 56, 73);
constexpr COLORREF kText = RGB(238, 243, 251);
constexpr COLORREF kMuted = RGB(143, 158, 181);
constexpr COLORREF kGrid = RGB(39, 48, 62);
constexpr COLORREF kBlue = RGB(86, 180, 255);
constexpr COLORREF kGreen = RGB(86, 214, 154);
constexpr COLORREF kAmber = RGB(241, 187, 84);
constexpr COLORREF kRose = RGB(243, 111, 142);
constexpr COLORREF kPurple = RGB(173, 126, 255);
constexpr COLORREF kCyan = RGB(83, 215, 220);

constexpr size_t kHistorySamples = 120; // ~4s at 33 ms polling.

struct AnalyzerState
{
  OhlAnalyzerMetrics raw{};
  OhlAnalyzerMetrics smooth{};
  bool initialized = false;

  std::deque<float> historyAmbience;
  std::deque<float> historyCenter;
  std::deque<float> historyRear;
  std::deque<float> historyBins;
};

float Clamp01(float x)
{
  return std::clamp(x, 0.0f, 1.0f);
}

float SmoothValue(float current, float target, float attack, float release)
{
  const float a = target > current ? attack : release;
  return current + a * (target - current);
}

void PushHistory(std::deque<float>& q, float v)
{
  q.push_back(Clamp01(v));
  while (q.size() > kHistorySamples)
    q.pop_front();
}

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

void Text(HDC dc, HFONT font, COLORREF color, const wchar_t* value, RECT r, UINT flags)
{
  HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  DrawTextW(dc, value, -1, &r, flags);
  if (old) SelectObject(dc, old);
}

void DrawMeter(HDC dc,
               HFONT font,
               const wchar_t* label,
               float value,
               int x,
               int y,
               int w,
               COLORREF color)
{
  value = Clamp01(value);

  wchar_t pct[32] = {};
  swprintf_s(pct, L"%d%%", static_cast<int>(std::lround(value * 100.0f)));

  RECT labelRect{x, y, x + 88, y + 19};
  Text(dc, font, kMuted, label, labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  RECT pctRect{x + w - 44, y, x + w, y + 19};
  Text(dc, font, kText, pct, pctRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

  RECT track{x + 88, y + 6, x + w - 52, y + 13};
  FillRound(dc, track, 6, RGB(38, 47, 61));

  RECT fill = track;
  fill.right = fill.left + static_cast<int>(
      std::lround(static_cast<double>(track.right - track.left) * value));
  if (fill.right > fill.left)
    FillRound(dc, fill, 6, color);
}

void DrawBandBars(HDC dc,
                  HFONT font,
                  const std::array<float, 4>& ownership,
                  const std::array<float, 4>& center,
                  int x,
                  int y,
                  int w)
{
  static const wchar_t* names[4] = {L"LOW", L"BODY", L"PRES", L"AIR"};

  RECT title{x, y, x + w, y + 18};
  Text(dc, font, kMuted, L"SPECTRAL OWNERSHIP", title,
       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  const int top = y + 24;
  const int gap = 8;
  const int cellW = (w - gap * 3) / 4;

  for (int i = 0; i < 4; ++i)
  {
    const int cx = x + i * (cellW + gap);

    RECT name{cx, top, cx + cellW, top + 17};
    Text(dc, font, kText, names[i], name, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT ownBg{cx, top + 22, cx + cellW, top + 31};
    RECT ctrBg{cx, top + 37, cx + cellW, top + 46};
    FillRound(dc, ownBg, 5, RGB(37, 46, 59));
    FillRound(dc, ctrBg, 5, RGB(37, 46, 59));

    RECT ownFill = ownBg;
    ownFill.right = ownFill.left + static_cast<int>(
        std::lround((ownFill.right - ownFill.left) * Clamp01(ownership[i])));
    if (ownFill.right > ownFill.left) FillRound(dc, ownFill, 5, kBlue);

    RECT ctrFill = ctrBg;
    ctrFill.right = ctrFill.left + static_cast<int>(
        std::lround((ctrFill.right - ctrFill.left) * Clamp01(center[i])));
    if (ctrFill.right > ctrFill.left) FillRound(dc, ctrFill, 5, kPurple);
  }

  RECT legend{x, top + 52, x + w, top + 69};
  Text(dc, font, kMuted, L"blue = rear ownership     purple = center confidence",
       legend, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void DrawHistory(HDC dc,
                 HFONT font,
                 const AnalyzerState& s,
                 int x,
                 int y,
                 int w,
                 int h)
{
  RECT title{x, y, x + w, y + 18};
  Text(dc, font, kMuted, L"~4 SECOND HISTORY", title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  RECT graph{x, y + 22, x + w, y + h};
  FillRound(dc, graph, 8, RGB(17, 22, 30));

  HPEN grid = CreatePen(PS_SOLID, 1, kGrid);
  HGDIOBJ oldP = SelectObject(dc, grid);
  for (int i = 1; i < 4; ++i)
  {
    const int gy = graph.top + (graph.bottom - graph.top) * i / 4;
    MoveToEx(dc, graph.left + 1, gy, nullptr);
    LineTo(dc, graph.right - 1, gy);
  }
  SelectObject(dc, oldP);
  DeleteObject(grid);

  const auto drawTrace = [&](const std::deque<float>& q, COLORREF color)
  {
    if (q.size() < 2)
      return;

    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);

    for (size_t i = 0; i < q.size(); ++i)
    {
      const double tx = q.size() <= 1
          ? 0.0
          : static_cast<double>(i) / static_cast<double>(q.size() - 1);
      const int px = graph.left + 2 + static_cast<int>(
          std::lround(tx * std::max(1, graph.right - graph.left - 4)));
      const int py = graph.bottom - 2 - static_cast<int>(
          std::lround(Clamp01(q[i]) * std::max(1, graph.bottom - graph.top - 4)));

      if (i == 0) MoveToEx(dc, px, py, nullptr);
      else LineTo(dc, px, py);
    }

    SelectObject(dc, oldPen);
    DeleteObject(pen);
  };

  drawTrace(s.historyAmbience, kBlue);
  drawTrace(s.historyCenter, kPurple);
  drawTrace(s.historyRear, kGreen);
  drawTrace(s.historyBins, kAmber);

  RECT legend{graph.left + 8, graph.top + 5, graph.right - 8, graph.top + 22};
  Text(dc, font, kMuted, L"AMB   CTR   REAR   BINS", legend,
       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK AnalyzerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  auto* s = reinterpret_cast<AnalyzerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_NCCREATE)
  {
    s = new AnalyzerState();
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
  }

  switch (msg)
  {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
    {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      RECT r{};
      GetClientRect(hwnd, &r);

      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1, r.right), std::max(1, r.bottom));
      HGDIOBJ oldBmp = SelectObject(mem, bmp);

      FillRectColor(mem, r, kBg);
      RECT card = r;
      FillRound(mem, card, 12, kCard);

      HPEN border = CreatePen(PS_SOLID, 1, kBorder);
      HGDIOBJ oldPen = SelectObject(mem, border);
      HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
      RoundRect(mem, card.left, card.top, card.right - 1, card.bottom - 1, 12, 12);
      SelectObject(mem, oldBrush);
      SelectObject(mem, oldPen);
      DeleteObject(border);

      HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
      RECT title{16, 10, r.right - 16, 31};
      Text(mem, font, kText, L"LIVE SPATIAL ANALYZER", title,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      const bool online = s && s->raw.online;
      const COLORREF dotColor = online ? kGreen : kRose;
      HBRUSH dot = CreateSolidBrush(dotColor);
      HGDIOBJ oldDot = SelectObject(mem, dot);
      HGDIOBJ oldDotPen = SelectObject(mem, GetStockObject(NULL_PEN));
      Ellipse(mem, r.right - 26, 14, r.right - 16, 24);
      SelectObject(mem, oldDotPen);
      SelectObject(mem, oldDot);
      DeleteObject(dot);

      if (!s || !online)
      {
        RECT offline{16, 48, r.right - 16, r.bottom - 16};
        Text(mem, font, kMuted, L"Engine offline / waiting for telemetry",
             offline, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }
      else
      {
        const auto& m = s->smooth;
        const int meterX = 16;
        const int meterW = r.right - 32;
        DrawMeter(mem, font, L"Ambience", m.ambience, meterX, 40, meterW, kBlue);
        DrawMeter(mem, font, L"Center", m.center, meterX, 62, meterW, kPurple);
        DrawMeter(mem, font, L"Spatial", m.spatialBins, meterX, 84, meterW, kAmber);
        DrawMeter(mem, font, L"Transient", m.transient, meterX, 106, meterW, kRose);
        DrawMeter(mem, font, L"Rear open", m.rearOpen, meterX, 128, meterW, kGreen);
        DrawMeter(mem, font, L"Front lock", m.frontLock, meterX, 150, meterW, kCyan);

        wchar_t budget[96] = {};
        swprintf_s(budget, L"rear budget scale  %d%%",
                   static_cast<int>(std::lround(Clamp01(m.budgetScale) * 100.0f)));
        RECT br{16, 174, r.right - 16, 192};
        Text(mem, font, m.budgetScale < 0.98f ? kAmber : kMuted, budget, br,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        DrawBandBars(mem, font, m.ownership, m.bandCenter, 16, 198, meterW);
        DrawHistory(mem, font, *s, 16, 282, meterW, std::max(70, r.bottom - 296));
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

bool RegisterOhlAnalyzerVisual(HINSTANCE instance)
{
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = AnalyzerProc;
  wc.hInstance = instance;
  wc.lpszClassName = kOhlAnalyzerClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND CreateOhlAnalyzerVisual(HWND parent, int id, int x, int y, int w, int h)
{
  return CreateWindowW(
      kOhlAnalyzerClass, L"", WS_CHILD | WS_VISIBLE,
      x, y, w, h, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
}

void UpdateOhlAnalyzerVisual(HWND hwnd, const OhlAnalyzerMetrics& metrics)
{
  if (!hwnd)
    return;

  auto* s = reinterpret_cast<AnalyzerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!s)
    return;

  s->raw = metrics;

  if (!s->initialized)
  {
    s->smooth = metrics;
    s->initialized = true;
  }
  else
  {
    // Fast attack, gentler decay. The UI feels responsive without turning packet-to-packet
    // classifier noise into unreadable twitching.
    s->smooth.online = metrics.online;
    s->smooth.ambience = SmoothValue(s->smooth.ambience, metrics.ambience, 0.55f, 0.28f);
    s->smooth.center = SmoothValue(s->smooth.center, metrics.center, 0.55f, 0.28f);
    s->smooth.spatialBins = SmoothValue(s->smooth.spatialBins, metrics.spatialBins, 0.55f, 0.28f);
    s->smooth.transient = SmoothValue(s->smooth.transient, metrics.transient, 0.70f, 0.40f);
    s->smooth.rearOpen = SmoothValue(s->smooth.rearOpen, metrics.rearOpen, 0.55f, 0.25f);
    s->smooth.frontLock = SmoothValue(s->smooth.frontLock, metrics.frontLock, 0.55f, 0.28f);
    s->smooth.budgetScale = SmoothValue(s->smooth.budgetScale, metrics.budgetScale, 0.45f, 0.30f);

    for (size_t i = 0; i < 4; ++i)
    {
      s->smooth.ownership[i] =
          SmoothValue(s->smooth.ownership[i], metrics.ownership[i], 0.58f, 0.24f);
      s->smooth.bandCenter[i] =
          SmoothValue(s->smooth.bandCenter[i], metrics.bandCenter[i], 0.58f, 0.24f);
    }
  }

  if (metrics.online)
  {
    PushHistory(s->historyAmbience, s->smooth.ambience);
    PushHistory(s->historyCenter, s->smooth.center);
    PushHistory(s->historyRear, s->smooth.rearOpen);
    PushHistory(s->historyBins, s->smooth.spatialBins);
  }

  InvalidateRect(hwnd, nullptr, FALSE);
}
