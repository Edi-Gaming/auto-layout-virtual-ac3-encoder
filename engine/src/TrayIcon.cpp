#include "TrayIcon.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

constexpr wchar_t kTrayClass[] = L"VirtualAc3EncoderTrayWindow";
constexpr UINT kTrayCallback = WM_APP + 42;
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kTrayId = 1;

constexpr UINT kMenuSurround = 2001;
constexpr UINT kMenuGuitar = 2002;
constexpr UINT kMenuSwitcher = 2003;
constexpr UINT kMenuLog = 2004;
constexpr UINT kMenuExit = 2005;

std::wstring WidenUtf8(const std::string& s)
{
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

HICON IconForMode(RuntimeAudioMode mode)
{
  // Keep v1 extremely robust by using Windows-owned stock icons. The tray behavior is now
  // day-to-day infrastructure; a branded OHL resource icon can replace these later without
  // touching the control architecture.
  switch (mode)
  {
    case RuntimeAudioMode::Surround:
      return LoadIconW(nullptr, MAKEINTRESOURCEW(OIC_INFORMATION));
    case RuntimeAudioMode::Guitar:
      return LoadIconW(nullptr, MAKEINTRESOURCEW(OIC_SAMPLE));
    case RuntimeAudioMode::Starting:
    case RuntimeAudioMode::Stopping:
      return LoadIconW(nullptr, MAKEINTRESOURCEW(OIC_QUES));
    case RuntimeAudioMode::Error:
      return LoadIconW(nullptr, MAKEINTRESOURCEW(OIC_ERROR));
  }
  return LoadIconW(nullptr, MAKEINTRESOURCEW(OIC_SAMPLE));
}

std::wstring TooltipForMode(RuntimeAudioMode mode)
{
  switch (mode)
  {
    case RuntimeAudioMode::Surround:
      return L"OHL Virtual AC3 Encoder - SURROUND";
    case RuntimeAudioMode::Guitar:
      return L"OHL Virtual AC3 Encoder - GUITAR (S/PDIF released)";
    case RuntimeAudioMode::Starting:
      return L"OHL Virtual AC3 Encoder - acquiring S/PDIF...";
    case RuntimeAudioMode::Stopping:
      return L"OHL Virtual AC3 Encoder - releasing S/PDIF...";
    case RuntimeAudioMode::Error:
      return L"OHL Virtual AC3 Encoder - ERROR";
  }
  return L"OHL Virtual AC3 Encoder";
}

std::wstring ModeLabel(RuntimeAudioMode mode)
{
  switch (mode)
  {
    case RuntimeAudioMode::Surround: return L"SURROUND active - AC-3 owns S/PDIF";
    case RuntimeAudioMode::Guitar: return L"GUITAR active - S/PDIF free for ASIO4ALL";
    case RuntimeAudioMode::Starting: return L"Starting surround pipeline...";
    case RuntimeAudioMode::Stopping: return L"Releasing S/PDIF...";
    case RuntimeAudioMode::Error: return L"ERROR - surround pipeline unavailable";
  }
  return L"Unknown state";
}

} // namespace

TrayIcon::~TrayIcon()
{
  Stop();
}

bool TrayIcon::Start(std::atomic<RuntimeAudioMode>* desired,
                     std::atomic<RuntimeAudioMode>* current,
                     std::string* lastError,
                     std::mutex* errorMutex,
                     std::atomic_bool* stopEngine,
                     std::atomic_int* requestedExitCode,
                     const std::string& logPath)
{
  if (thread_.joinable())
    return true;

  desired_ = desired;
  current_ = current;
  lastError_ = lastError;
  errorMutex_ = errorMutex;
  stopEngine_ = stopEngine;
  requestedExitCode_ = requestedExitCode;
  logPath_ = logPath;
  stop_.store(false);

  try
  {
    thread_ = std::thread(&TrayIcon::ThreadProc, this);
  }
  catch (...)
  {
    return false;
  }
  return true;
}

void TrayIcon::Stop()
{
  if (!thread_.joinable())
    return;

  stop_.store(true);
  if (hwnd_)
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
  thread_.join();
}

void TrayIcon::AddIcon()
{
  if (!hwnd_)
    return;

  NOTIFYICONDATAW nid = {};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd_;
  nid.uID = kTrayId;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = kTrayCallback;

  RuntimeAudioMode mode = current_ ? current_->load() : RuntimeAudioMode::Error;
  nid.hIcon = IconForMode(mode);
  const std::wstring tip = TooltipForMode(mode);
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

  if (Shell_NotifyIconW(NIM_ADD, &nid))
  {
    iconAdded_ = true;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    lastMode_ = mode;
  }
  else
  {
    std::fprintf(stderr, "[Tray] Shell_NotifyIcon(NIM_ADD) failed: %lu\n", GetLastError());
  }
}

void TrayIcon::RemoveIcon()
{
  if (!iconAdded_ || !hwnd_)
    return;

  NOTIFYICONDATAW nid = {};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd_;
  nid.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  iconAdded_ = false;
}

void TrayIcon::UpdateIcon(bool force)
{
  if (!hwnd_)
    return;

  RuntimeAudioMode mode = current_ ? current_->load() : RuntimeAudioMode::Error;
  if (!force && iconAdded_ && mode == lastMode_)
    return;

  if (!iconAdded_)
  {
    AddIcon();
    return;
  }

  NOTIFYICONDATAW nid = {};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd_;
  nid.uID = kTrayId;
  nid.uFlags = NIF_ICON | NIF_TIP;
  nid.hIcon = IconForMode(mode);
  const std::wstring tip = TooltipForMode(mode);
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

  if (Shell_NotifyIconW(NIM_MODIFY, &nid))
    lastMode_ = mode;
}

void TrayIcon::LaunchSwitcher()
{
  wchar_t exe[MAX_PATH] = {};
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH))
    return;

  HINSTANCE rc = ShellExecuteW(nullptr, L"open", exe, L"--switcher", nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(rc) <= 32)
    std::fprintf(stderr, "[Tray] failed to launch switcher\n");
}

void TrayIcon::OpenLog()
{
  if (logPath_.empty())
    return;

  const std::wstring log = WidenUtf8(logPath_);
  HINSTANCE rc = ShellExecuteW(nullptr, L"open", L"notepad.exe", log.c_str(), nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(rc) <= 32)
    std::fprintf(stderr, "[Tray] failed to open log\n");
}

void TrayIcon::ShowContextMenu()
{
  if (!hwnd_)
    return;

  RuntimeAudioMode mode = current_ ? current_->load() : RuntimeAudioMode::Error;

  HMENU menu = CreatePopupMenu();
  if (!menu)
    return;

  const std::wstring status = ModeLabel(mode);
  AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, status.c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  UINT surroundFlags = MF_STRING;
  UINT guitarFlags = MF_STRING;
  if (mode == RuntimeAudioMode::Surround) surroundFlags |= MF_CHECKED;
  if (mode == RuntimeAudioMode::Guitar) guitarFlags |= MF_CHECKED;

  AppendMenuW(menu, surroundFlags, kMenuSurround, L"Surround mode");
  AppendMenuW(menu, guitarFlags, kMenuGuitar, L"Guitar / low-latency mode");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | MF_DEFAULT, kMenuSwitcher, L"Open mode switcher...");
  AppendMenuW(menu, logPath_.empty() ? (MF_STRING | MF_GRAYED) : MF_STRING,
              kMenuLog, L"Open engine log");

  if (mode == RuntimeAudioMode::Error && lastError_ && errorMutex_)
  {
    std::string err;
    {
      std::lock_guard<std::mutex> lock(*errorMutex_);
      err = *lastError_;
    }
    if (!err.empty())
    {
      std::wstring werr = L"Error: " + WidenUtf8(err);
      if (werr.size() > 90)
        werr.resize(90);
      AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, werr.c_str());
    }
  }

  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit engine");

  POINT pt = {};
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd_);
  UINT cmd = TrackPopupMenu(menu,
                            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                            pt.x,
                            pt.y,
                            0,
                            hwnd_,
                            nullptr);
  DestroyMenu(menu);
  PostMessageW(hwnd_, WM_NULL, 0, 0);

  switch (cmd)
  {
    case kMenuSurround:
      if (desired_) desired_->store(RuntimeAudioMode::Surround);
      break;
    case kMenuGuitar:
      if (desired_) desired_->store(RuntimeAudioMode::Guitar);
      break;
    case kMenuSwitcher:
      LaunchSwitcher();
      break;
    case kMenuLog:
      OpenLog();
      break;
    case kMenuExit:
      // Exit code 10 tells the Startup supervisor this was an intentional tray quit,
      // so it must not immediately restart the engine.
      if (requestedExitCode_) requestedExitCode_->store(10);
      if (stopEngine_) stopEngine_->store(true);
      break;
  }
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  TrayIcon* self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_NCCREATE)
  {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    self = static_cast<TrayIcon*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }

  if (self && self->taskbarCreatedMsg_ && msg == self->taskbarCreatedMsg_)
  {
    self->iconAdded_ = false;
    self->AddIcon();
    return 0;
  }

  switch (msg)
  {
    case WM_TIMER:
      if (self && wp == kRefreshTimer)
      {
        self->UpdateIcon();
        return 0;
      }
      break;

    case kTrayCallback:
      if (self)
      {
        const UINT event = LOWORD(lp);
        if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU)
        {
          self->ShowContextMenu();
          return 0;
        }
        if (event == WM_LBUTTONDBLCLK || event == NIN_SELECT || event == NIN_KEYSELECT)
        {
          self->LaunchSwitcher();
          return 0;
        }
      }
      break;

    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      if (self)
      {
        KillTimer(hwnd, kRefreshTimer);
        self->RemoveIcon();
        self->hwnd_ = nullptr;
      }
      PostQuitMessage(0);
      return 0;
  }

  return DefWindowProcW(hwnd, msg, wp, lp);
}

void TrayIcon::ThreadProc()
{
  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSW wc = {};
  wc.lpfnWndProc = &TrayIcon::WndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kTrayClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

  if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
  {
    std::fprintf(stderr, "[Tray] RegisterClass failed: %lu\n", GetLastError());
    return;
  }

  taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

  hwnd_ = CreateWindowExW(
      0,
      kTrayClass,
      L"OHL Virtual AC3 Encoder Tray",
      WS_OVERLAPPED,
      0, 0, 0, 0,
      nullptr,
      nullptr,
      instance,
      this);

  if (!hwnd_)
  {
    std::fprintf(stderr, "[Tray] CreateWindow failed: %lu\n", GetLastError());
    return;
  }

  AddIcon();
  SetTimer(hwnd_, kRefreshTimer, 500, nullptr);
  std::printf("[Tray] ready\n");

  MSG msg = {};
  while (!stop_.load() && GetMessageW(&msg, nullptr, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  RemoveIcon();
  if (hwnd_)
  {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}
