// Config.h — engine runtime configuration (CLI-driven for now; file-based in Phase 4).
#pragma once

#include <string>

struct Config
{
  bool listDevices = false;
  bool probe = false;    // probe all render endpoints for AC3 passthrough support, then exit
  bool loopback = false; // capture the input as a RENDER endpoint via WASAPI loopback
                         // (the render-only virtual-driver architecture)
  bool monitor = false;  // capture-only diagnostic: report input throughput, then exit
  int  monitorSeconds = 5;
  int  durationSeconds = 0; // 0 = run until Ctrl+C; otherwise auto-stop after N seconds

  // Capture (input) endpoint — the virtual cable's recording side.
  std::wstring inName = L"CABLE Output"; // friendly-name substring (VB-CABLE default)
  std::wstring inId;                     // exact endpoint id (overrides inName)

  // Output endpoint — the optical / Toslink device.
  std::wstring outName;  // friendly-name substring
  std::wstring outId;    // exact endpoint id (overrides outName / auto)
  bool outAutoSpdif = false; // pick the first render endpoint with SPDIF form factor

  int64_t  bitRate = 640000;
  uint32_t safeFrames = 1536;

  // Stereo->5.1 upmix mode for <=2ch input in fixed-5.1 mode: "off" (swr default) or
  // "surround" (FFmpeg `surround` FFT upmix). In auto layout, genuine stereo is encoded
  // as AC3 2.0 instead so the receiver can apply its own PLII/A.F.D. processing.
  std::string upmix = "surround";

  // AC3 payload layout:
  //   "auto" — inspect actual PCM activity; encode 2.0 until C/LFE/surround becomes active,
  //            then switch to 5.1 and hold it until those channels stay quiet long enough.
  //   "5.1"  — preserve upstream behavior: always encode AC3 5.1.
  // This fork defaults to auto because that is its purpose; set layout=5.1 for compatibility.
  std::string layout = "auto";
  double   autoThresholdDb = -60.0;
  uint32_t autoHoldMs = 2000;
};
