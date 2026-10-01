#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <windows.h>

#include "AnalyzerVisual.h"

bool RegisterOhlStageVisual(HINSTANCE instance);
HWND CreateOhlStageVisual(HWND parent, int id, int x, int y, int w, int h);

void UpdateOhlStageVisual(HWND hwnd,
                          const std::array<float, 5>& distancesInches,
                          const OhlAnalyzerMetrics& metrics);

constexpr wchar_t kOhlStageClass[] = L"OhlStageVisual";
