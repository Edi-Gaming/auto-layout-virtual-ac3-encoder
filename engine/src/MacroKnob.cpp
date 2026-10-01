#include "MacroKnob.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr COLORREF kBg = RGB(15, 17, 23);
constexpr COLORREF kCard = RGB(20, 25, 34);
constexpr COLORREF kCardHot = RGB(25, 31, 42);
constexpr COLORREF kBorder = RGB(45, 56, 73);
constexpr COLORREF kTrack = RGB(43, 53, 69);
constexpr COLORREF kText = RGB(238, 243, 251);
constexpr COLORREF kMuted = RGB(143, 158, 181);
constexpr COLORREF kAccentDefault = RGB(88, 181, 255);

struct KnobState
{
  int min = 0;
  int max = 100;
  int pos = 0;
  bool dragging = false;
  bool hover = false;
  int lastY = 0;
  COLORREF accent = kAccentDefault;
};

double Normalize(const KnobState& s)
{
  if (s.max <= s.min) return 0.0;
  return std::clamp(
      static_cast<double>(s.pos - s.min) / static_cast<double>(s.max - s.min),
      0.0, 1.0);
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

void Notify(HWND hwnd, const KnobState& s, int code)
{
  SendMessageW(
      GetParent(hwnd),
      WM_HSCROLL,
      MAKEWPARAM(code, static_cast<WORD>(s.pos)),
      reinterpret_cast<LPARAM>(hwnd));
}

void SetPos(HWND hwnd, KnobState& s, int pos, bool notify)
{
  const int next = std::clamp(pos, s.min, s.max);
  if (next == s.pos) return;
  s.pos = next;
  InvalidateRect(hwnd, nullptr, FALSE);
  if (notify) Notify(hwnd, s, TB_THUMBTRACK);
}

std::pair<std::wstring, std::wstring> SplitCaption(HWND hwnd)
{
  wchar_t buf[256] = {};
  GetWindowTextW(hwnd, buf, 256);
  std::wstring all(buf);
  const size_t sep = all.find(L'\n');
  if (sep == std::wstring::npos)
    return {all, L""};
  return {all.substr(0, sep), all.substr(sep + 1)};
}

POINT ArcPoint(int cx, int cy, int radius, double degrees)
{
  const double rad = degrees * kPi / 180.0;
  return {
      cx + static_cast<LONG>(std::lround(radius * std::cos(rad))),
      cy + static_cast<LONG>(std::lround(radius * std::sin(rad)))};
}

void DrawArcSegments(HDC dc,
                     int cx,
                     int cy,
                     int radius,
                     double startDeg,
                     double endDeg,
                     int segments,
                     int width,
                     COLORREF color)
{
  HPEN pen = CreatePen(PS_SOLID, width, color);
  HGDIOBJ old = SelectObject(dc, pen);

  POINT previous = ArcPoint(cx, cy, radius, startDeg);
  for (int i = 1; i <= segments; ++i)
  {
    const double t = static_cast<double>(i) / static_cast<double>(segments);
    const double deg = startDeg + (endDeg - startDeg) * t;
    const POINT p = ArcPoint(cx, cy, radius, deg);
    MoveToEx(dc, previous.x, previous.y, nullptr);
    LineTo(dc, p.x, p.y);
    previous = p;
  }

  SelectObject(dc, old);
  DeleteObject(pen);
}

LRESULT CALLBACK KnobProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  auto* s = reinterpret_cast<KnobState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE)
  {
    s = new KnobState();
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
  }

  switch (msg)
  {
    case TBM_SETRANGE:
      if (s)
      {
        s->min = static_cast<short>(LOWORD(lp));
        s->max = static_cast<short>(HIWORD(lp));
        if (s->max < s->min) std::swap(s->min, s->max);
        s->pos = std::clamp(s->pos, s->min, s->max);
        if (wp) InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case TBM_SETPOS:
      if (s)
      {
        s->pos = std::clamp(static_cast<int>(lp), s->min, s->max);
        if (wp) InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case TBM_GETPOS:
      return s ? s->pos : 0;

    case OHL_KNOB_SET_ACCENT:
      if (s)
      {
        s->accent = static_cast<COLORREF>(lp);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_MOUSEMOVE:
      if (s)
      {
        if (!s->hover)
        {
          s->hover = true;
          TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
          TrackMouseEvent(&tme);
          InvalidateRect(hwnd, nullptr, FALSE);
        }

        if (s->dragging)
        {
          const int y = GET_Y_LPARAM(lp);
          const int delta = s->lastY - y;
          if (std::abs(delta) >= 2)
          {
            SetPos(hwnd, *s, s->pos + delta / 2, true);
            s->lastY = y;
          }
        }
      }
      return 0;

    case WM_MOUSELEAVE:
      if (s)
      {
        s->hover = false;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_LBUTTONDOWN:
      if (s)
      {
        SetFocus(hwnd);
        SetCapture(hwnd);
        s->dragging = true;
        s->lastY = GET_Y_LPARAM(lp);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_LBUTTONUP:
      if (s && s->dragging)
      {
        s->dragging = false;
        ReleaseCapture();
        Notify(hwnd, *s, TB_ENDTRACK);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_MOUSEWHEEL:
      if (s)
      {
        const int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        SetPos(hwnd, *s, s->pos + steps, true);
      }
      return 0;

    case WM_KEYDOWN:
      if (s)
      {
        int next = s->pos;
        if (wp == VK_LEFT || wp == VK_DOWN) --next;
        else if (wp == VK_RIGHT || wp == VK_UP) ++next;
        else if (wp == VK_PRIOR) next += 5;
        else if (wp == VK_NEXT) next -= 5;
        else if (wp == VK_HOME) next = s->min;
        else if (wp == VK_END) next = s->max;
        else break;
        SetPos(hwnd, *s, next, true);
      }
      return 0;

    case WM_GETDLGCODE:
      return DLGC_WANTARROWS;

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

      HBRUSH bg = CreateSolidBrush(kBg);
      FillRect(mem, &r, bg);
      DeleteObject(bg);

      const COLORREF cardColor =
          s && (s->hover || s->dragging) ? kCardHot : kCard;
      FillRound(mem, r, 14, cardColor);

      HPEN border = CreatePen(
          PS_SOLID, 1,
          s && s->hover ? Blend(kBorder, s->accent, 0.65f) : kBorder);
      HGDIOBJ oldPen = SelectObject(mem, border);
      HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
      RoundRect(mem, r.left, r.top, r.right - 1, r.bottom - 1, 14, 14);
      SelectObject(mem, oldBrush);
      SelectObject(mem, oldPen);
      DeleteObject(border);

      if (s)
      {
        const auto [title, subtitle] = SplitCaption(hwnd);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font ? SelectObject(mem, font) : nullptr;
        SetBkMode(mem, TRANSPARENT);

        RECT titleRect{14, 12, r.right - 14, 34};
        SetTextColor(mem, kText);
        DrawTextW(mem, title.c_str(), -1, &titleRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT subtitleRect{14, 34, r.right - 14, 58};
        SetTextColor(mem, kMuted);
        DrawTextW(mem, subtitle.c_str(), -1, &subtitleRect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

        const int cx = static_cast<int>(r.right) / 2;
        const int cy = 108;
        const int radius = 37;
        constexpr double startDeg = 135.0;
        constexpr double sweepDeg = 270.0;

        DrawArcSegments(mem, cx, cy, radius, startDeg, startDeg + sweepDeg,
                        52, 7, kTrack);

        const double t = Normalize(*s);
        if (t > 0.001)
          DrawArcSegments(mem, cx, cy, radius, startDeg,
                          startDeg + sweepDeg * t,
                          std::max(2, static_cast<int>(std::lround(52 * t))),
                          7, s->accent);

        const POINT pointer = ArcPoint(cx, cy, radius - 8, startDeg + sweepDeg * t);
        HPEN pointerPen = CreatePen(PS_SOLID, 3, s->accent);
        oldPen = SelectObject(mem, pointerPen);
        MoveToEx(mem, cx, cy, nullptr);
        LineTo(mem, pointer.x, pointer.y);
        SelectObject(mem, oldPen);
        DeleteObject(pointerPen);

        HBRUSH hub = CreateSolidBrush(Blend(RGB(35, 43, 56), s->accent, 0.25f));
        oldBrush = SelectObject(mem, hub);
        oldPen = SelectObject(mem, GetStockObject(NULL_PEN));
        Ellipse(mem, cx - 19, cy - 19, cx + 20, cy + 20);
        SelectObject(mem, oldPen);
        SelectObject(mem, oldBrush);
        DeleteObject(hub);

        wchar_t value[32] = {};
        swprintf_s(value, L"%d%%", s->pos);
        RECT valueRect{cx - 42, cy - 11, cx + 42, cy + 13};
        SetTextColor(mem, kText);
        DrawTextW(mem, value, -1, &valueRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT hintRect{14, r.bottom - 26, r.right - 14, r.bottom - 9};
        SetTextColor(mem, kMuted);
        DrawTextW(mem, L"drag vertically  •  wheel  •  arrows", -1, &hintRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (oldFont) SelectObject(mem, oldFont);
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

bool RegisterOhlMacroKnob(HINSTANCE instance)
{
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = KnobProc;
  wc.hInstance = instance;
  wc.lpszClassName = kOhlMacroKnobClass;
  wc.hCursor = LoadCursor(nullptr, IDC_SIZENS);
  wc.hbrBackground = nullptr;
  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
