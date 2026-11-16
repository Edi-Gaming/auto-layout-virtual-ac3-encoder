#include "GuitarWorkflow.h"
#include "ModeControl.h"

#include <commdlg.h>
#include <shellapi.h>
#include <chrono>
#include <string>
#include <thread>

namespace {
constexpr wchar_t kKey[] = L"Software\\OHL\\GuitarWorkflow";
constexpr wchar_t kValue[] = L"HostExecutable";
}

std::wstring GetGuitarHostPath()
{
  wchar_t path[32768] = {};
  DWORD bytes = sizeof(path);
  if (RegGetValueW(HKEY_CURRENT_USER, kKey, kValue,
                   RRF_RT_REG_SZ, nullptr, path, &bytes) != ERROR_SUCCESS)
    return {};
  return std::wstring(path);
}

bool ChooseGuitarHost(HWND owner, std::wstring& error)
{
  wchar_t path[32768] = {};
  const std::wstring existing = GetGuitarHostPath();
  if (!existing.empty() && existing.size() < _countof(path))
    wcscpy_s(path, existing.c_str());

  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFilter = L"Applications (*.exe)\0*.exe\0All files (*.*)\0*.*\0";
  dialog.lpstrFile = path;
  dialog.nMaxFile = _countof(path);
  dialog.lpstrTitle = L"Select the guitar / ASIO host executable";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog))
  {
    error = L"Selection cancelled.";
    return false;
  }

  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
  {
    error = L"Cannot save the guitar application for this Windows user.";
    return false;
  }
  const DWORD bytes = static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t));
  const LSTATUS status = RegSetValueExW(key, kValue, 0, REG_SZ,
                                       reinterpret_cast<const BYTE*>(path), bytes);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS)
  {
    error = L"Cannot save the selected guitar application.";
    return false;
  }
  return true;
}

bool LaunchGuitarSession(HWND owner, std::wstring& error)
{
  std::wstring path = GetGuitarHostPath();
  if (path.empty())
  {
    if (!ChooseGuitarHost(owner, error)) return false;
    path = GetGuitarHostPath();
  }

  const DWORD attrs = GetFileAttributesW(path.c_str());
  if (path.empty() || attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
  {
    error = L"The saved guitar application is missing. Select it again in OHL.";
    return false;
  }

  std::string response;
  if (!SendModeCommand("guitar", response, 1500) || response != "ok")
  {
    error = L"The OHL background engine is offline or refused Guitar mode.";
    return false;
  }

  // Acknowledging the command is not enough: wait until the render thread
  // and its exclusive WASAPI IAudioClient have actually been destroyed.
  bool released = false;
  for (int i = 0; i < 80; ++i)
  {
    if (SendModeCommand("status", response, 300) && response == "guitar")
    {
      released = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (!released)
  {
    error = L"OHL did not confirm S/PDIF release. Guitar application not started.";
    return false;
  }

  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.hwnd = owner;
  info.lpVerb = L"open";
  info.lpFile = path.c_str();
  const size_t slash = path.find_last_of(L"\\/");
  const std::wstring directory = slash == std::wstring::npos ? L"" : path.substr(0, slash);
  info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info))
  {
    const DWORD launchError = GetLastError();
    std::string ignored;
    SendModeCommand("surround", ignored, 1000);
    error = L"Could not launch the selected application (Windows error " +
            std::to_wstring(launchError) + L").";
    return false;
  }

  if (info.hProcess)
  {
    const DWORD pid = GetProcessId(info.hProcess);
    CloseHandle(info.hProcess);
    if (pid)
    {
      std::string ignored;
      // Engine owns the session, so closing the switcher does not cancel it.
      SendModeCommand("guitar session " + std::to_string(pid), ignored, 1000);
    }
  }
  return true;
}
