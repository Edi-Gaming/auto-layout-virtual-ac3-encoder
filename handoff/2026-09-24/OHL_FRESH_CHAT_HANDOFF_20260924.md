# OHL / Virtual AC3 Encoder — fresh-chat handoff — 2026-09-24

## 1. Project identity and hard rules

Repository:

`Edi-Gaming/auto-layout-virtual-ac3-encoder`

Active development branch:

`feature/auto-layout`

Open pull request:

PR #1 — **Add automatic AC3 2.0 / 5.1 layout, runtime modes, and tray control**

Base branch:

`main`

**Do not use `main` as source of truth. Do not merge PR #1 unless Edi explicitly asks.** The release workflow can be affected by merging.

At handoff creation time, the branch immediately before the handoff package was:

`f8ded3528ff22d74114bd881d1b1d2c26de2d6fc`

The last code-changing commit before this handoff was:

`4143e389163feb2d20bb8b2c6344eae1304e0d23` — **Detach hidden daemon from console**

CI run #49 on `f8ded...` passed. The handoff package is documentation-only; a fresh session must fetch the current remote HEAD again before editing.

System-level companion documentation is in:

- repo: `Edi-Gaming/SONYEXPOLRATION`
- file: `docs/15_LOW_LATENCY_GUITAR_MODE_2026-09-22.md`
- latest relevant documentation commit at handoff time: `8c97ac4a6460c79d23444c2837321c9265c540ff`

## 2. What the project now does

This is a persistent Windows optical-audio transport/controller for Edi's Sony STR-K900 system.

Normal SURROUND path:

`Windows apps -> VB-CABLE -> OHL engine -> AC-3 / IEC61937 -> Realtek S/PDIF -> Sony STR-K900`

GUITAR path:

`guitar -> pedal -> Realtek line input -> ASIO4ALL -> AmpliTube -> direct PCM S/PDIF -> Sony STR-K900`

The engine has two runtime ownership modes:

- **SURROUND**: OHL owns the optical output and runs the AC-3 pipeline.
- **GUITAR**: OHL destroys capture/output objects and releases S/PDIF completely so ASIO4ALL can acquire it.

The background engine remains the control-plane owner in both modes.

## 3. Auto AC-3 2.0 / 5.1 layout

The core feature is receiver-friendly payload switching without reopening the optical endpoint.

The engine keeps separate stereo and 5.1 FFmpeg AC-3 encoder contexts alive and switches the AC-3 payload layout according to non-front-channel activity.

Default configuration:

```ini
in=CABLE Output
out=Realtek Digital Output
bitrate=640000
loopback=0
layout=auto
auto_threshold_db=-60
auto_hold_ms=2000
tray=1
```

Behavior:

- Starts in AC-3 2.0.
- Any center/LFE/surround activity above the threshold immediately switches to AC-3 5.1.
- After approximately 2000 ms of quiet non-front content, it returns to AC-3 2.0.
- AC-3 frame duration at 48 kHz is 1536 / 48000 = 32 ms, so observed switch-back timing naturally lands near the hold time plus one frame.
- The IEC60958 carrier remains continuous; the Sony learns the layout from AC-3 metadata/`acmod`.
- Exact zero non-front samples are required for stereo; sufficiently strong rear noise/dither can keep the stream in 5.1.

This was hardware-validated earlier on the actual Sony setup with correct LFE, narration, stereo/5.1 transitions, and no click/pop.

## 4. Runtime mode-control architecture

Named pipe:

`\\.\pipe\virtual-ac3-encoder-mode-v1`

Commands:

```powershell
engine.exe --mode surround
engine.exe --mode guitar
engine.exe --mode status
```

Native controller:

```powershell
engine.exe --switcher
```

Daemon singleton mutex:

`Local\VirtualAc3EncoderEngineV1`

Important ownership rule:

`RunningPipeline` destroys objects in output/capture/ring order so COM/WASAPI references are actually released. Calling only `Stop()` was not enough for the low-latency handoff requirement.

If SURROUND cannot reacquire S/PDIF, the engine records an error and retries every two seconds.

Hardware validation already proved essentially immediate SURROUND <-> GUITAR handoff with ASIO4ALL/AmpliTube and the Sony optical input.

## 5. Tray and OHL UI

The persistent engine owns a Windows notification-area controller.

Tray features:

- Surround mode
- Guitar / low-latency mode
- Open mode switcher
- Open engine log
- Exit engine
- double-click opens switcher
- tooltip follows runtime state
- re-add after Explorer/taskbar restart

The tray/window icon is an embedded compact crop of Edi's actual holographic OHL emblem. No external image is required at runtime.

The mode switcher is native Win32, dark themed, OHL branded, and uses real CRLF multiline labels.

OHL means **Optical High-Fidelity Link**.

## 6. Day-to-day install architecture

Normal installed root:

`%LOCALAPPDATA%\virtual-ac3-encoder`

Config:

`%LOCALAPPDATA%\virtual-ac3-encoder\virtual-ac3-encoder.conf`

Log:

`%LOCALAPPDATA%\virtual-ac3-encoder\engine.log`

The CI artifact includes:

- `engine.exe`
- FFmpeg/runtime DLLs
- bundled MSVC runtime DLLs
- `INSTALL-DAY-TO-DAY.cmd`
- `install-day-to-day.ps1`

The day-to-day installer preserves an existing config.

It removes known legacy startup mechanisms from the old upstream install, including:

- old install root: `%LOCALAPPDATA%\Virtual AC3 Encoder`
- legacy Startup VBS/LNK entries
- legacy scheduled-task names
- matching HKCU Run values
- matching old/new encoder processes during migration

## 7. IMPORTANT: latest startup/console fix

A reboot test exposed a new problem after the old legacy startup path was successfully removed:

Windows Startup was directly launching console-subsystem `engine.exe`. A command-prompt window remained visible. Closing that window killed the encoder.

This was reproduced by Edi on the real PC.

The fix is now committed but **had not yet been hardware-validated by Edi at the moment this handoff was written**.

Current intended startup chain:

```text
OHL Virtual AC3 Encoder.lnk
    -> wscript.exe
    -> %LOCALAPPDATA%\virtual-ac3-encoder\OHL-Autostart.vbs
    -> exact installed engine.exe --hidden --log ...
```

The VBS is intentionally a **one-shot launcher only**:

- no watchdog
- no restart loop
- no supervisor semantics
- launches the exact current OHL engine hidden
- exits immediately

The installer also creates a Start Menu recovery shortcut:

**OHL Virtual AC3 Encoder -> Start OHL Encoder**

Additionally, `engine.exe --hidden` now:

1. parses the log path,
2. redirects stdout/stderr,
3. hides any inherited console,
4. calls `FreeConsole()`.

This means the daemon should no longer remain attached to a launch console, and closing an unrelated console should not terminate it.

Relevant commits:

- `bd90f7c6275534314b56b7ec7db0dffe9ff0a154` — hidden one-shot launcher in day-to-day installer
- `e8e094718d342e4b4e66d1afab43e0a8ed0aa75e` — same startup strategy in setup script
- `4143e389163feb2d20bb8b2c6344eae1304e0d23` — detach hidden daemon from console
- `f8ded3528ff22d74114bd881d1b1d2c26de2d6fc` — document the new startup architecture

CI run #49 passed on `f8ded...`.

## 8. Latest CI artifact before the handoff package

CI run:

#49 — run id `36077552234`

Head:

`f8ded3528ff22d74114bd881d1b1d2c26de2d6fc`

Conclusion:

**success**

Artifact:

- name: `virtual-ac3-encoder-day-to-day`
- artifact id: `10840319801`
- size: 68,585,464 bytes
- digest: `sha256:e54067cf3da46c3cd0fd7f6677785fbf24f79238d743f0c06fc57eec5259df78`
- expiry reported by GitHub: 2026-12-24

Do not reuse old ChatGPT sandbox links. Fresh sessions should download the current GitHub Actions artifact or build from the branch.

## 9. What was physically validated versus only CI-validated

Physically validated on Edi's hardware:

- automatic AC-3 2.0 / 5.1 switching
- Sony recognizes both layouts
- PLII remains available on AC-3 2.0
- LFE / narration / channel mapping behavior
- fast SURROUND -> GUITAR release
- fast GUITAR -> SURROUND reacquisition
- ASIO4ALL/AmpliTube direct PCM optical path
- branded tray and switcher
- day-to-day engine operation
- legacy old-fixed-5.1 startup was identified and migrated away

Not yet physically revalidated at handoff time:

- the final hidden one-shot `OHL-Autostart.vbs` startup after reboot
- `FreeConsole()` behavior on the real PC across reboot
- Start Menu **Start OHL Encoder** recovery shortcut

The very next test should be: install the newest artifact, confirm no permanent console window, confirm tray appears, confirm AC-3 works, then reboot and repeat.

## 10. Audio decisions already made

Edi is running 48 kHz throughout and is satisfied with AC-3 transport.

A hybrid transport design — PCM 2.0 for stereo and AC-3 5.1 for surround — was considered and deliberately deprioritized because it would require endpoint teardown/reopen, receiver carrier relock, and additional race/pop/failure modes.

Current decision:

**Keep continuous AC-3 transport. Do not push hybrid PCM unless Edi explicitly reopens that idea.**

The stereo path is intentionally true AC-3 2.0 because Edi is heavily using and enjoying Dolby Pro Logic II Music on the Sony.

## 11. Practical next feature candidates

After the startup fix is hardware-confirmed, the best practical candidates are:

- sleep/resume robustness
- endpoint/device-loss recovery
- richer tray status showing actual payload state: AC-3 2.0 / AC-3 5.1 / GUITAR / error
- live layout diagnostics: non-front peak dBFS, threshold, hold timer
- optional automatic Guitar mode when AmpliTube starts
- global hotkeys / Windows notifications
- built-in speaker diagnostics
- profiles

DTS remains experimental/future.

## 12. Files most likely to matter

Core runtime:

- `engine/src/main.cpp`
- `engine/src/ModeControl.cpp`
- `engine/src/ModeControl.h`
- `engine/src/TrayIcon.cpp`
- `engine/src/TrayIcon.h`
- `engine/src/WasapiPassthrough.cpp`
- `engine/src/WasapiPassthrough.h`
- `engine/src/SpdifEncoder.cpp`
- `engine/src/SpdifEncoder.h`
- `engine/src/Config.h`

Branding:

- `engine/src/BrandIcon.cpp`
- `engine/src/BrandIcon.h`

Install/startup:

- `scripts/install-day-to-day.ps1`
- `scripts/install-day-to-day.cmd`
- `scripts/setup-autostart.ps1`
- `scripts/remove-autostart.ps1`
- `installer/virtual-ac3-encoder.iss`
- `installer/stop-engine.ps1`
- `installer/supervisor.vbs`

Build/CI:

- `.github/workflows/ci.yml`
- `.github/workflows/release.yml`
- `engine/CMakeLists.txt`

## 13. Fresh-session discipline

Before any mutation:

- fetch current `feature/auto-layout` HEAD
- fetch the exact file being edited and its blob SHA
- do not assume the handoff HEAD is still current
- keep changes on `feature/auto-layout`
- do not modify unrelated encoder behavior
- run/observe CI after source changes
- checkpoint frequently
- never invent a commit hash or claim hardware validation that did not happen

If a new build is needed, prefer the CI artifact from the latest green run and verify its head SHA before telling Edi it contains a specific fix.
