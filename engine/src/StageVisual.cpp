#include "StageVisual.h"

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

struct StageState
{
  std::array<float, 5> distances{{33.0f, 30.0f, 33.0f, 27.0f, 33.0f}};
  OhlAnalyzerMetrics metrics{};
};

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

void DrawSpeaker(HDC dc,
                 HFONT font,
                 int cx,
                 int cy,
                 const wchar_t* name,
                 float inches,
                 float activity,
                 COLORREF activeColor)
{
  activity = std::clamp(activity, 0.0f, 1.0f);
  const COLORREF shell = Blend(RGB(33, 42, 55), activeColor, 0.22f + 0.55f * activity);
  RECT body{cx - 20, cy - 25, cx + 20, cy + 19};
  FillRound(dc, body, 8, shell);

  HPEN border = CreatePen(PS_SOLID, 1, Blend(kBorder, activeColor, activity));
  HGDIOBJ oldP = SelectObject(dc, border);
  HGDIOBJ oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
  RoundRect(dc, body.left, body.top, body.right - 1, body.bottom - 1, 8, 8);
  SelectObject(dc, oldB);
  SelectObject(dc, oldP);
  DeleteObject(border);

  HBRUSH woofer = CreateSolidBrush(Blend(RGB(25, 31, 41), activeColor, 0.45f * activity));
  oldB = SelectObject(dc, woofer);
  oldP = SelectObject(dc, GetStockObject(NULL_PEN));
  Ellipse(dc, cx - 9, cy - 11, cx + 9, cy + 7);
  SelectObject(dc, oldP);
  SelectObject(dc, oldB);
  DeleteObject(woofer);

  RECT nameRect{cx - 35, cy + 23, cx + 35, cy + 41};
  DrawTextSimple(dc, font, kText, name, nameRect,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  wchar_t dist[32] = {};
  swprintf_s(dist, L"%.0f in", inches);
  RECT distRect{cx - 35, cy + 39, cx + 35, cy + 56};
  DrawTextSimple(dc, font, kMuted, dist, distRect,
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
      FillRound(mem, r, 12, kCard);

      HPEN border = CreatePen(PS_SOLID, 1, kBorder);
      HGDIOBJ oldP = SelectObject(mem, border);
      HGDIOBJ oldB = SelectObject(mem, GetStockObject(NULL_BRUSH));
      RoundRect(mem, r.left, r.top, r.right - 1, r.bottom - 1, 12, 12);
      SelectObject(mem, oldB);
      SelectObject(mem, oldP);
      DeleteObject(border);

      HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
      RECT title{16, 9, r.right - 16, 29};
      DrawTextSimple(mem, font, kText, L"LISTENING STAGE", title,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      if (!s)
      {
        BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
      }

      const int w = static_cast<int>(r.right - r.left);
      const int h = static_cast<int>(r.bottom - r.top);
      const POINT head{w / 2, std::max(120, h * 58 / 100)};

      const POINT fl{58, 72};
      const POINT fc{w / 2, 58};
      const POINT fr{w - 58, 72};
      const POINT sl{62, std::min(h - 60, static_cast<int>(head.y) + 85)};
      const POINT sr{w - 62, std::min(h - 60, static_cast<int>(head.y) + 85)};

      const float rearActivity = std::clamp(
          0.58f * s->metrics.rearOpen +
          0.42f * (s->metrics.ownership[1] + s->metrics.ownership[2] +
                   s->metrics.ownership[3]) / 3.0f,
          0.0f, 1.0f);
      const float centerActivity = std::clamp(s->metrics.center, 0.0f, 1.0f);
      const float field = std::clamp(
          0.6f * s->metrics.ambience + 0.4f * s->metrics.spatialBins,
          0.0f, 1.0f);

      // Spatial field rings around the listener.
      HPEN fieldPen = CreatePen(PS_SOLID, 2, Blend(kGrid, kBlue, field));
      oldP = SelectObject(mem, fieldPen);
      oldB = SelectObject(mem, GetStockObject(NULL_BRUSH));
      const int ring = 36 + static_cast<int>(std::lround(22.0f * field));
      Ellipse(mem, head.x - ring, head.y - ring, head.x + ring, head.y + ring);
      SelectObject(mem, oldB);
      SelectObject(mem, oldP);
      DeleteObject(fieldPen);

      // Signal paths.
      DrawConnection(mem, head, fl, Blend(kGrid, kBlue, 0.35f), 1);
      DrawConnection(mem, head, fc, Blend(kGrid, kPurple, 0.35f + 0.50f * centerActivity), 1);
      DrawConnection(mem, head, fr, Blend(kGrid, kBlue, 0.35f), 1);
      DrawConnection(mem, head, sl, Blend(kGrid, kGreen, 0.25f + 0.70f * rearActivity), rearActivity > 0.45f ? 2 : 1);
      DrawConnection(mem, head, sr, Blend(kGrid, kGreen, 0.25f + 0.70f * rearActivity), rearActivity > 0.45f ? 2 : 1);

      DrawSpeaker(mem, font, fl.x, fl.y, L"FL", s->distances[0], 0.35f, kBlue);
      DrawSpeaker(mem, font, fc.x, fc.y, L"C", s->distances[1], centerActivity, kPurple);
      DrawSpeaker(mem, font, fr.x, fr.y, L"FR", s->distances[2], 0.35f, kBlue);
      DrawSpeaker(mem, font, sl.x, sl.y, L"SL", s->distances[3], rearActivity, kGreen);
      DrawSpeaker(mem, font, sr.x, sr.y, L"SR", s->distances[4], rearActivity, kGreen);

      // Listener head, facing the front stage.
      HBRUSH headBrush = CreateSolidBrush(RGB(44, 52, 66));
      oldB = SelectObject(mem, headBrush);
      oldP = SelectObject(mem, GetStockObject(NULL_PEN));
      Ellipse(mem, head.x - 20, head.y - 22, head.x + 20, head.y + 22);
      SelectObject(mem, oldP);
      SelectObject(mem, oldB);
      DeleteObject(headBrush);

      HPEN face = CreatePen(PS_SOLID, 2, kText);
      oldP = SelectObject(mem, face);
      MoveToEx(mem, head.x, head.y - 16, nullptr);
      LineTo(mem, head.x, head.y - 28);
      MoveToEx(mem, head.x - 6, head.y - 11, nullptr);
      LineTo(mem, head.x - 10, head.y - 18);
      MoveToEx(mem, head.x + 6, head.y - 11, nullptr);
      LineTo(mem, head.x + 10, head.y - 18);
      SelectObject(mem, oldP);
      DeleteObject(face);

      RECT listener{head.x - 42, head.y + 26, head.x + 42, head.y + 44};
      DrawTextSimple(mem, font, kMuted, L"LISTENER", listener,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      wchar_t fieldText[64] = {};
      swprintf_s(fieldText, L"spatial field  %d%%",
                 static_cast<int>(std::lround(field * 100.0f)));
      RECT fieldRect{16, h - 26, w - 16, h - 8};
      DrawTextSimple(mem, font, field > 0.35f ? kGreen : kMuted, fieldText, fieldRect,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

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
      kOhlStageClass, L"", WS_CHILD | WS_VISIBLE,
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
  s->metrics = metrics;
  InvalidateRect(hwnd, nullptr, FALSE);
}
