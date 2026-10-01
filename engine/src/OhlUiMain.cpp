#include "ModeControl.h"
#include "MusicSettings.h"
#include "SurroundWizard.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

std::string ExeDirUtf8()
{
  wchar_t path[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH)
    return ".";

  std::wstring p(path, n);
  const size_t slash = p.find_last_of(L"\\/");
  if (slash == std::wstring::npos)
    return ".";

  const std::wstring dir = p.substr(0, slash);
  if (dir.empty())
    return ".";

  const int bytes = WideCharToMultiByte(
      CP_UTF8, 0, dir.c_str(), static_cast<int>(dir.size()), nullptr, 0, nullptr, nullptr);
  if (bytes <= 0)
    return ".";

  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, dir.c_str(), static_cast<int>(dir.size()), out.data(), bytes, nullptr, nullptr);
  return out;
}

bool HasArg(int argc, wchar_t** argv, const wchar_t* wanted)
{
  for (int i = 1; i < argc; ++i)
    if (_wcsicmp(argv[i], wanted) == 0)
      return true;
  return false;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv)
    return 1;

  int rc = 0;
  if (HasArg(argc, argv, L"--music-settings"))
  {
    rc = RunMusicSettingsGui(ExeDirUtf8() + "\\virtual-ac3-encoder.conf");
  }
  else if (HasArg(argc, argv, L"--surround-wizard"))
  {
    rc = RunSurroundWizardGui();
  }
  else
  {
    // Default action is deliberately the mode switcher so Start Menu / tray launches need no args.
    rc = RunModeSwitcherGui();
  }

  LocalFree(argv);
  return rc;
}
