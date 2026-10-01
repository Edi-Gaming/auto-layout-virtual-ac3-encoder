#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

bool RegisterOhlModernControls(HINSTANCE instance);

constexpr wchar_t kOhlSliderClass[] = L"OhlModernSlider";
constexpr wchar_t kOhlButtonClass[] = L"OhlModernButton";
constexpr wchar_t kOhlGroupClass[] = L"OhlModernGroup";
constexpr wchar_t kOhlCardLabelClass[] = L"OhlCardLabel";
