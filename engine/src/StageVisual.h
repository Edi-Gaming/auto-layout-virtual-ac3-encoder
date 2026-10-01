#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <windows.h>

#include "AnalyzerVisual.h"

enum class OhlStageSpeaker : int
{
  FL = 0,
  C = 1,
  FR = 2,
  SL = 3,
  SR = 4,
};

constexpr UINT OHL_STAGE_DISTANCE_CHANGED = WM_APP + 122;

bool RegisterOhlStageVisual(HINSTANCE instance);
HWND CreateOhlStageVisual(HWND parent, int id, int x, int y, int w, int h);

void UpdateOhlStageVisual(HWND hwnd,
                          const std::array<float, 5>& distancesInches,
                          const OhlAnalyzerMetrics& metrics);

constexpr wchar_t kOhlStageClass[] = L"OhlStageVisual";
