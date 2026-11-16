#pragma once
#include <windows.h>
#include <string>

// Per-user selected host path; ASIO4ALL's own per-host settings are never modified.
std::wstring GetGuitarHostPath();
bool ChooseGuitarHost(HWND owner, std::wstring& error);
// Releases OHL's exclusive optical endpoint before starting the selected ASIO host.
bool LaunchGuitarSession(HWND owner, std::wstring& error);
