#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

bool RegisterOhlMacroKnob(HINSTANCE instance);

constexpr wchar_t kOhlMacroKnobClass[] = L"OhlMacroKnob";
constexpr UINT OHL_KNOB_SET_ACCENT = WM_USER + 230;
