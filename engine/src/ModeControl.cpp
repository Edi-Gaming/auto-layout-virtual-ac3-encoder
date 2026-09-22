#include "ModeControl.h"
#include "BrandIcon.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\virtual-ac3-encoder-mode-v1";
constexpr wchar_t kWindowClass[] = L"VirtualAc3EncoderModeSwitcher";
constexpr int kIdSurround = 1001;
constexpr int kIdGuitar = 1002;
constexpr int kIdStatus = 1003;
constexpr int kIdHint = 1004;
constexpr int kIdTitle = 1005;
constexpr int kIdSubtitle = 1006;

HBRUSH gBackgroundBrush = nullptr;
HFONT gTitleFont = nullptr;
HFONT gUiFont = nullptr;
HFONT gButtonFont = nullptr;
constexpr UINT_PTR kStatusTimer = 1;

std::wstring WidenUtf8(const std::string& s)
{
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

void SetStatus(HWND hwnd, const std::string& text)
{
  HWND status = GetDlgItem(hwnd, kIdStatus);
  if (!status) return;

  std::wstring w;
  if (text == "surround")
    w = L"SURROUND ACTIVE  -  encoder owns S/PDIF";
  else if (text == "guitar")
    w = L"GUITAR ACTIVE  -  S/PDIF released for ASIO4ALL";
  else if (text == "starting")
    w = L"Starting surround engine...";
  else if (text == "stopping")
    w = L"Releasing S/PDIF...";
  else if (text.rfind("error:", 0) == 0)
    w = L"ERROR  -  " + WidenUtf8(text.substr(6));
  else if (text == "offline")
    w = L"ENGINE OFFLINE  -  start the background engine first";
  else
    w = WidenUtf8(text);

  SetWindowTextW(status, w.c_str());
}

void RefreshStatus(HWND hwnd)
{
  std::string response;
  if (!SendModeCommand("status", response, 250))
    response = "offline";
  SetStatus(hwnd, response);
}

void RequestMode(HWND hwnd, const char* mode)
{
  std::string response;
  if (!SendModeCommand(mode, response, 1000))
  {
    SetStatus(hwnd, "offline");
    MessageBoxW(hwnd,
                L"The background virtual-ac3-encoder engine is not reachable.\n\n"
                L"Start the installed engine, then try again.",
                L"OHL Audio Mode",
                MB_OK | MB_ICONERROR);
    return;
  }
  RefreshStatus(hwnd);
}

LRESULT CALLBACK SwitcherWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_CREATE:
    {
      gTitleFont = CreateFontW(-27, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      gUiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      gButtonFont = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

      HWND title = CreateWindowW(L"STATIC", L"OHL  |  AUDIO MODE",
                                 WS_CHILD | WS_VISIBLE,
                                 22, 17, 520, 34,
                                 hwnd, reinterpret_cast<HMENU>(kIdTitle), nullptr, nullptr);
      SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(gTitleFont), TRUE);

      HWND sub = CreateWindowW(L"STATIC",
                               L"Optical High-Fidelity Link  |  Sony STR-K900",
                               WS_CHILD | WS_VISIBLE,
                               23, 52, 520, 23,
                               hwnd, reinterpret_cast<HMENU>(kIdSubtitle), nullptr, nullptr);
      SendMessageW(sub, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);

      HWND status = CreateWindowW(L"STATIC", L"Checking engine...",
                                  WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | WS_BORDER,
                                  22, 84, 520, 44,
                                  hwnd, reinterpret_cast<HMENU>(kIdStatus), nullptr, nullptr);
      SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);

      HWND surround = CreateWindowW(L"BUTTON",
                                    L"SURROUND\r\nVB-CABLE  >  AC-3  >  S/PDIF",
                                    WS_CHILD | WS_VISIBLE | BS_MULTILINE | BS_PUSHBUTTON,
                                    22, 146, 250, 82,
                                    hwnd, reinterpret_cast<HMENU>(kIdSurround), nullptr, nullptr);
      SendMessageW(surround, WM_SETFONT, reinterpret_cast<WPARAM>(gButtonFont), TRUE);

      HWND guitar = CreateWindowW(L"BUTTON",
                                  L"GUITAR\r\nASIO4ALL  >  direct S/PDIF",
                                  WS_CHILD | WS_VISIBLE | BS_MULTILINE | BS_PUSHBUTTON,
                                  292, 146, 250, 82,
                                  hwnd, reinterpret_cast<HMENU>(kIdGuitar), nullptr, nullptr);
      SendMessageW(guitar, WM_SETFONT, reinterpret_cast<WPARAM>(gButtonFont), TRUE);

      HWND hint = CreateWindowW(
          L"STATIC",
          L"Surround owns optical for AC-3. Guitar releases S/PDIF for ASIO4ALL; "
          L"the engine itself stays running.",
          WS_CHILD | WS_VISIBLE,
          22, 246, 520, 48,
          hwnd, reinterpret_cast<HMENU>(kIdHint), nullptr, nullptr);
      SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);

      SetTimer(hwnd, kStatusTimer, 750, nullptr);
      RefreshStatus(hwnd);
      return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
      HDC dc = reinterpret_cast<HDC>(wp);
      HWND control = reinterpret_cast<HWND>(lp);
      const int id = GetDlgCtrlID(control);
      SetBkMode(dc, TRANSPARENT);
      if (id == kIdSubtitle || id == kIdHint)
        SetTextColor(dc, RGB(160, 170, 188));
      else
        SetTextColor(dc, RGB(238, 242, 250));
      return reinterpret_cast<LRESULT>(gBackgroundBrush);
    }

    case WM_COMMAND:
      switch (LOWORD(wp))
      {
        case kIdSurround: RequestMode(hwnd, "surround"); return 0;
        case kIdGuitar:   RequestMode(hwnd, "guitar");   return 0;
      }
      break;

    case WM_TIMER:
      if (wp == kStatusTimer)
      {
        RefreshStatus(hwnd);
        return 0;
      }
      break;

    case WM_DESTROY:
      KillTimer(hwnd, kStatusTimer);
      if (gTitleFont) { DeleteObject(gTitleFont); gTitleFont = nullptr; }
      if (gUiFont) { DeleteObject(gUiFont); gUiFont = nullptr; }
      if (gButtonFont) { DeleteObject(gButtonFont); gButtonFont = nullptr; }
      if (gBackgroundBrush) { DeleteObject(gBackgroundBrush); gBackgroundBrush = nullptr; }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

const char* RuntimeAudioModeName(RuntimeAudioMode mode)
{
  switch (mode)
  {
    case RuntimeAudioMode::Surround: return "surround";
    case RuntimeAudioMode::Guitar:   return "guitar";
    case RuntimeAudioMode::Starting: return "starting";
    case RuntimeAudioMode::Stopping: return "stopping";
    case RuntimeAudioMode::Error:    return "error";
  }
  return "error";
}

bool SendModeCommand(const std::string& command, std::string& response, unsigned timeoutMs)
{
  response.clear();

  if (!WaitNamedPipeW(kPipeName, timeoutMs))
    return false;

  HANDLE pipe = CreateFileW(kPipeName,
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            nullptr,
                            OPEN_EXISTING,
                            0,
                            nullptr);
  if (pipe == INVALID_HANDLE_VALUE)
    return false;

  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

  DWORD written = 0;
  const DWORD bytes = static_cast<DWORD>(command.size());
  bool ok = WriteFile(pipe, command.data(), bytes, &written, nullptr) != FALSE && written == bytes;

  char buf[1024] = {};
  DWORD read = 0;
  if (ok)
    ok = ReadFile(pipe, buf, static_cast<DWORD>(sizeof(buf) - 1), &read, nullptr) != FALSE;

  if (ok)
  {
    buf[std::min<DWORD>(read, static_cast<DWORD>(sizeof(buf) - 1))] = 0;
    response.assign(buf, read);
  }

  CloseHandle(pipe);
  return ok;
}

ModeControlServer::~ModeControlServer()
{
  Stop();
}

bool ModeControlServer::Start(std::atomic<RuntimeAudioMode>* desired,
                              std::atomic<RuntimeAudioMode>* current,
                              std::string* lastError,
                              std::mutex* errorMutex)
{
  if (thread_.joinable())
    return true;

  desired_ = desired;
  current_ = current;
  lastError_ = lastError;
  errorMutex_ = errorMutex;
  stop_.store(false);

  try
  {
    thread_ = std::thread(&ModeControlServer::ThreadProc, this);
  }
  catch (...)
  {
    return false;
  }
  return true;
}

void ModeControlServer::Stop()
{
  if (!thread_.joinable())
    return;

  stop_.store(true);
  std::string ignored;
  SendModeCommand("__stop", ignored, 100);
  thread_.join();
}

void ModeControlServer::ThreadProc()
{
  while (!stop_.load())
  {
    HANDLE pipe = CreateNamedPipeW(
        kPipeName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        1024,
        1024,
        0,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE)
    {
      std::fprintf(stderr, "[ModeControl] CreateNamedPipe failed: %lu\n", GetLastError());
      return;
    }

    BOOL connected = ConnectNamedPipe(pipe, nullptr)
                         ? TRUE
                         : (GetLastError() == ERROR_PIPE_CONNECTED);

    if (!connected)
    {
      CloseHandle(pipe);
      if (stop_.load()) break;
      continue;
    }

    char buf[256] = {};
    DWORD read = 0;
    std::string response = "error:bad command";

    if (ReadFile(pipe, buf, static_cast<DWORD>(sizeof(buf) - 1), &read, nullptr))
    {
      buf[std::min<DWORD>(read, static_cast<DWORD>(sizeof(buf) - 1))] = 0;
      std::string command(buf, read);

      if (command == "surround")
      {
        desired_->store(RuntimeAudioMode::Surround);
        response = "ok";
      }
      else if (command == "guitar")
      {
        desired_->store(RuntimeAudioMode::Guitar);
        response = "ok";
      }
      else if (command == "status")
      {
        RuntimeAudioMode state = current_->load();
        if (state == RuntimeAudioMode::Error)
        {
          std::string err;
          if (lastError_ && errorMutex_)
          {
            std::lock_guard<std::mutex> lock(*errorMutex_);
            err = *lastError_;
          }
          response = "error:" + err;
        }
        else
        {
          response = RuntimeAudioModeName(state);
        }
      }
      else if (command == "__stop")
      {
        response = "ok";
      }
    }

    DWORD written = 0;
    WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &written, nullptr);
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);

    if (stop_.load())
      break;
  }
}

int RunModeSwitcherGui()
{
  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSW wc = {};
  wc.lpfnWndProc = SwitcherWndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kWindowClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = GetOhlBrandIcon();
  wc.hIconSm = GetOhlBrandIcon();
  gBackgroundBrush = CreateSolidBrush(RGB(15, 17, 23));
  wc.hbrBackground = gBackgroundBrush;

  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(
      0,
      kWindowClass,
      L"OHL Audio Mode Switcher",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      580,
      340,
      nullptr,
      nullptr,
      instance,
      nullptr);

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
