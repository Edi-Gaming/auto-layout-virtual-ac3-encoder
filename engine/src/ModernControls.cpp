#include "ModernControls.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr COLORREF kBg = RGB(15, 17, 23);
constexpr COLORREF kCard = RGB(20, 25, 34);
constexpr COLORREF kCardHot = RGB(27, 34, 46);
constexpr COLORREF kBorder = RGB(45, 56, 73);
constexpr COLORREF kTrack = RGB(47, 58, 75);
constexpr COLORREF kAccent = RGB(88, 181, 255);
constexpr COLORREF kAccentHot = RGB(124, 201, 255);
constexpr COLORREF kText = RGB(236, 242, 251);
constexpr COLORREF kMuted = RGB(154, 169, 190);

struct SliderState
{
  int min = 0;
  int max = 100;
  int pos = 0;
  bool dragging = false;
  bool hover = false;
};

struct ButtonState
{
  bool hover = false;
  bool pressed = false;
};

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

int SliderX(const RECT& r, const SliderState& s)
{
  const int pad = 9;
  const int left = r.left + pad;
  const int right = std::max(left + 1, r.right - pad);
  const double t = s.max == s.min
      ? 0.0
      : static_cast<double>(s.pos - s.min) / static_cast<double>(s.max - s.min);
  return left + static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * (right - left)));
}

void SetSliderFromX(HWND hwnd, SliderState& s, int x, bool notify)
{
  RECT r{};
  GetClientRect(hwnd, &r);
  const int pad = 9;
  const int left = r.left + pad;
  const int right = std::max(left + 1, r.right - pad);
  const double t = std::clamp(
      static_cast<double>(x - left) / static_cast<double>(right - left), 0.0, 1.0);
  const int next = s.min + static_cast<int>(std::lround(t * (s.max - s.min)));
  if (next == s.pos)
    return;

  s.pos = std::clamp(next, s.min, s.max);
  InvalidateRect(hwnd, nullptr, FALSE);
  if (notify)
  {
    HWND parent = GetParent(hwnd);
    SendMessageW(parent, WM_HSCROLL,
                 MAKEWPARAM(TB_THUMBTRACK, static_cast<WORD>(s.pos)),
                 reinterpret_cast<LPARAM>(hwnd));
  }
}

LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  auto* s = reinterpret_cast<SliderState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_NCCREATE)
  {
    s = new SliderState();
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
  }

  switch (msg)
  {
    case TBM_SETRANGE:
      if (s)
      {
        s->min = static_cast<short>(LOWORD(lp));
        s->max = static_cast<short>(HIWORD(lp));
        if (s->max < s->min) std::swap(s->max, s->min);
        s->pos = std::clamp(s->pos, s->min, s->max);
        if (wp) InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case TBM_SETRANGEMIN:
      if (s) { s->min = static_cast<int>(lp); s->pos = std::max(s->pos, s->min); if (wp) InvalidateRect(hwnd, nullptr, FALSE); }
      return 0;

    case TBM_SETRANGEMAX:
      if (s) { s->max = static_cast<int>(lp); s->pos = std::min(s->pos, s->max); if (wp) InvalidateRect(hwnd, nullptr, FALSE); }
      return 0;

    case TBM_SETPOS:
      if (s) { s->pos = std::clamp(static_cast<int>(lp), s->min, s->max); if (wp) InvalidateRect(hwnd, nullptr, FALSE); }
      return 0;

    case TBM_GETPOS:
      return s ? s->pos : 0;

    case WM_LBUTTONDOWN:
      if (s)
      {
        SetFocus(hwnd);
        SetCapture(hwnd);
        s->dragging = true;
        SetSliderFromX(hwnd, *s, GET_X_LPARAM(lp), true);
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
          SetSliderFromX(hwnd, *s, GET_X_LPARAM(lp), true);
      }
      return 0;

    case WM_MOUSELEAVE:
      if (s) { s->hover = false; InvalidateRect(hwnd, nullptr, FALSE); }
      return 0;

    case WM_LBUTTONUP:
      if (s && s->dragging)
      {
        s->dragging = false;
        ReleaseCapture();
        SetSliderFromX(hwnd, *s, GET_X_LPARAM(lp), true);
        HWND parent = GetParent(hwnd);
        SendMessageW(parent, WM_HSCROLL,
                     MAKEWPARAM(TB_ENDTRACK, static_cast<WORD>(s->pos)),
                     reinterpret_cast<LPARAM>(hwnd));
      }
      return 0;

    case WM_KEYDOWN:
      if (s)
      {
        int next = s->pos;
        if (wp == VK_LEFT || wp == VK_DOWN) --next;
        else if (wp == VK_RIGHT || wp == VK_UP) ++next;
        else if (wp == VK_HOME) next = s->min;
        else if (wp == VK_END) next = s->max;
        else break;
        s->pos = std::clamp(next, s->min, s->max);
        InvalidateRect(hwnd, nullptr, FALSE);
        SendMessageW(GetParent(hwnd), WM_HSCROLL,
                     MAKEWPARAM(TB_THUMBTRACK, static_cast<WORD>(s->pos)),
                     reinterpret_cast<LPARAM>(hwnd));
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
      HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1, r.right), std::max(1, r.bottom));
      HGDIOBJ old = SelectObject(mem, bmp);
      FillRect(mem, &r, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
      HBRUSH bg = CreateSolidBrush(kBg);
      FillRect(mem, &r, bg);
      DeleteObject(bg);

      if (s)
      {
        const int cy = (r.top + r.bottom) / 2;
        RECT track{r.left + 9, cy - 2, r.right - 9, cy + 3};
        FillRound(mem, track, 5, kTrack);

        const int x = SliderX(r, *s);
        RECT fill{track.left, track.top, std::max(track.left + 1, x), track.bottom};
        FillRound(mem, fill, 5, kAccent);

        const int radius = s->dragging ? 7 : 6;
        HBRUSH thumb = CreateSolidBrush(s->hover ? kAccentHot : kText);
        HGDIOBJ oldB = SelectObject(mem, thumb);
        HGDIOBJ oldP = SelectObject(mem, GetStockObject(NULL_PEN));
        Ellipse(mem, x - radius, cy - radius, x + radius + 1, cy + radius + 1);
        SelectObject(mem, oldP);
        SelectObject(mem, oldB);
        DeleteObject(thumb);
      }

      BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
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

LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  auto* s = reinterpret_cast<ButtonState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE)
  {
    s = new ButtonState();
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
  }

  switch (msg)
  {
    case WM_MOUSEMOVE:
      if (s && !s->hover)
      {
        s->hover = true;
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_MOUSELEAVE:
      if (s) { s->hover = false; InvalidateRect(hwnd, nullptr, FALSE); }
      return 0;

    case WM_LBUTTONDOWN:
      if (s)
      {
        SetFocus(hwnd);
        SetCapture(hwnd);
        s->pressed = true;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_LBUTTONUP:
      if (s)
      {
        const bool wasPressed = s->pressed;
        s->pressed = false;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT r{}; GetClientRect(hwnd, &r);
        if (wasPressed && PtInRect(&r, p))
          SendMessageW(GetParent(hwnd), WM_COMMAND,
                       MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                       reinterpret_cast<LPARAM>(hwnd));
      }
      return 0;

    case WM_KEYDOWN:
      if (s && wp == VK_SPACE) { s->pressed = true; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
      break;

    case WM_KEYUP:
      if (s && wp == VK_SPACE && s->pressed)
      {
        s->pressed = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        SendMessageW(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                     reinterpret_cast<LPARAM>(hwnd));
        return 0;
      }
      break;

    case WM_GETDLGCODE:
      return DLGC_BUTTON | DLGC_WANTCHARS;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
    {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      RECT r{}; GetClientRect(hwnd, &r);
      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1, r.right), std::max(1, r.bottom));
      HGDIOBJ old = SelectObject(mem, bmp);

      const COLORREF fill = s && s->pressed ? RGB(38, 72, 98) :
                            s && s->hover ? kCardHot : kCard;
      FillRound(mem, r, 8, fill);

      HPEN border = CreatePen(PS_SOLID, 1, s && s->hover ? kAccent : kBorder);
      HGDIOBJ oldP = SelectObject(mem, border);
      HGDIOBJ oldB = SelectObject(mem, GetStockObject(NULL_BRUSH));
      RoundRect(mem, r.left, r.top, r.right - 1, r.bottom - 1, 8, 8);
      SelectObject(mem, oldB);
      SelectObject(mem, oldP);
      DeleteObject(border);

      wchar_t text[256] = {};
      GetWindowTextW(hwnd, text, 256);
      HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
      HGDIOBJ oldF = font ? SelectObject(mem, font) : nullptr;
      SetBkMode(mem, TRANSPARENT);
      SetTextColor(mem, kText);
      RECT tr = r;
      DrawTextW(mem, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      if (oldF) SelectObject(mem, oldF);

      BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
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

LRESULT CALLBACK GroupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_NCHITTEST:
      return HTTRANSPARENT;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
    {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      RECT r{}; GetClientRect(hwnd, &r);
      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1, r.right), std::max(1, r.bottom));
      HGDIOBJ old = SelectObject(mem, bmp);

      HBRUSH bg = CreateSolidBrush(kBg);
      FillRect(mem, &r, bg);
      DeleteObject(bg);

      RECT card = r;
      card.top += 7;
      FillRound(mem, card, 10, kCard);
      HPEN border = CreatePen(PS_SOLID, 1, kBorder);
      HGDIOBJ oldP = SelectObject(mem, border);
      HGDIOBJ oldB = SelectObject(mem, GetStockObject(NULL_BRUSH));
      RoundRect(mem, card.left, card.top, card.right - 1, card.bottom - 1, 10, 10);
      SelectObject(mem, oldB);
      SelectObject(mem, oldP);
      DeleteObject(border);

      wchar_t text[256] = {};
      GetWindowTextW(hwnd, text, 256);
      HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
      HGDIOBJ oldF = font ? SelectObject(mem, font) : nullptr;
      SetBkMode(mem, OPAQUE);
      SetBkColor(mem, kBg);
      SetTextColor(mem, kText);
      RECT title{12, 0, r.right - 12, 22};
      DrawTextW(mem, text, -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      if (oldF) SelectObject(mem, oldF);

      BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(bmp);
      DeleteDC(mem);
      EndPaint(hwnd, &ps);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool RegisterClassSimple(HINSTANCE instance,
                         const wchar_t* name,
                         WNDPROC proc,
                         HCURSOR cursor)
{
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = proc;
  wc.hInstance = instance;
  wc.lpszClassName = name;
  wc.hCursor = cursor;
  wc.hbrBackground = nullptr;
  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

bool RegisterOhlModernControls(HINSTANCE instance)
{
  return RegisterClassSimple(instance, kOhlSliderClass, SliderProc, LoadCursor(nullptr, IDC_HAND)) &&
         RegisterClassSimple(instance, kOhlButtonClass, ButtonProc, LoadCursor(nullptr, IDC_HAND)) &&
         RegisterClassSimple(instance, kOhlGroupClass, GroupProc, LoadCursor(nullptr, IDC_ARROW));
}
