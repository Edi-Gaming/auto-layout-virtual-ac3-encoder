# OHL validation and recovery status — 2026-09-24

## Current critical test

The latest startup fix is CI-green but was not yet tested on Edi's PC when this package was created.

The previous installed build did this after reboot:

- launched the correct OHL encoder
- left a persistent command-prompt window open
- closing that window terminated the encoder

The latest branch fixes that with both a one-shot hidden launcher and `FreeConsole()` in hidden daemon mode.

### Next hardware validation sequence

1. Obtain the latest green `virtual-ac3-encoder-day-to-day` artifact from the current `feature/auto-layout` HEAD.
2. Extract it.
3. Run `INSTALL-DAY-TO-DAY.cmd`.
4. Confirm:
   - no persistent command window
   - OHL tray icon appears
   - SURROUND mode acquires S/PDIF
   - Sony shows/decodes AC-3 normally
   - mode switcher opens
   - Start Menu **Start OHL Encoder** shortcut exists
5. Exit the engine from the tray.
6. Use **Start OHL Encoder** and confirm it returns hidden.
7. Reboot Windows.
8. Confirm again:
   - no permanent console
   - tray icon appears automatically
   - correct current OHL engine is running
   - no old fixed-5.1 engine starts
   - AC-3 2.0/5.1 auto layout still works

If all of those pass, mark the hidden-startup fix hardware-validated in both this repo and `SONYEXPOLRATION`.

## Manual recovery command

If an installed engine is stopped before the new recovery shortcut has been installed:

```powershell
$dir = Join-Path $env:LOCALAPPDATA 'virtual-ac3-encoder'
Start-Process `
  -FilePath (Join-Path $dir 'engine.exe') `
  -ArgumentList ('--hidden --log "' + (Join-Path $dir 'engine.log') + '"') `
  -WorkingDirectory $dir `
  -WindowStyle Hidden
```

After the latest installer is applied, prefer the Start Menu **Start OHL Encoder** shortcut.

## Known install roots

Current OHL:

`%LOCALAPPDATA%\virtual-ac3-encoder`

Legacy upstream install that must not own startup:

`%LOCALAPPDATA%\Virtual AC3 Encoder`

Startup folder:

`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup`

Authoritative current startup shortcut:

`OHL Virtual AC3 Encoder.lnk`

Current hidden launcher:

`%LOCALAPPDATA%\virtual-ac3-encoder\OHL-Autostart.vbs`

## Last green run before handoff package

CI run #49:

- run id: `36077552234`
- head: `f8ded3528ff22d74114bd881d1b1d2c26de2d6fc`
- conclusion: success
- artifact id: `10840319801`
- digest: `sha256:e54067cf3da46c3cd0fd7f6677785fbf24f79238d743f0c06fc57eec5259df78`

The last code-changing commit in that run is `4143e389163feb2d20bb8b2c6344eae1304e0d23`.

## Already hardware-validated

- automatic AC-3 2.0 / 5.1 layout switching
- 48 kHz optical operation
- continuous AC-3 carrier behavior
- Sony STR-K900 layout recognition
- PLII Music availability from true AC-3 2.0
- SURROUND/GUITAR control pipe and native switcher
- complete S/PDIF release for ASIO4ALL
- fast return from Guitar to Surround
- OHL tray UI / branded switcher
- MSVC-runtime-complete portable artifact
- day-to-day config preservation
- removal of the old legacy fixed-5.1 startup path

## Validation language rule

Do not write “fixed on hardware” merely because CI passed. For the console-detachment/startup change, use:

**CI-validated, hardware validation pending**

until Edi confirms the reboot behavior.
