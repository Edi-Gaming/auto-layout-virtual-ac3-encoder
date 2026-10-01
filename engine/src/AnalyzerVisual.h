#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <windows.h>

struct OhlAnalyzerMetrics
{
  bool online = false;

  float ambience = 0.0f;
  float center = 0.0f;
  float spatialBins = 0.0f;
  float transient = 0.0f;
  float rearOpen = 0.0f;
  float frontLock = 0.0f;
  float budgetScale = 1.0f;

  std::array<float, 4> ownership{{0, 0, 0, 0}};
  std::array<float, 4> bandCenter{{0, 0, 0, 0}};
};

bool RegisterOhlAnalyzerVisual(HINSTANCE instance);
HWND CreateOhlAnalyzerVisual(HWND parent, int id, int x, int y, int w, int h);
void UpdateOhlAnalyzerVisual(HWND hwnd, const OhlAnalyzerMetrics& metrics);

constexpr wchar_t kOhlAnalyzerClass[] = L"OhlAnalyzerVisual";
