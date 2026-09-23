<#
.SYNOPSIS
  "Set and forget": installs the engine to a stable per-user location, writes a config file,
  and autostarts it hidden at logon via a direct Startup-folder shortcut to this engine.

  Uses the Startup folder (not Task Scheduler): it runs in the real interactive logon session
  where WASAPI + a hidden console work, and needs NO elevation.

  Defaults target the VB-CABLE source -> Realtek optical output.

.EXAMPLE
  scripts\setup-autostart.ps1
  scripts\setup-autostart.ps1 -In "CABLE Output" -Out "Realtek Digital Output" -Bitrate 640000
  scripts\setup-autostart.ps1 -In "Virtual AC3 Encoder" -Loopback   # when using our own driver
#>
[CmdletBinding()]
param(
  [string]$In        = "CABLE Output",
  [string]$Out       = "Realtek Digital Output",
  [int]   $Bitrate   = 640000,
  [switch]$Loopback,
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'virtual-ac3-encoder'),
  [string]$BuildDir   = (Join-Path $PSScriptRoot '..\engine\build\Release')
)
$ErrorActionPreference = 'Stop'

$engineSrc = Join-Path $BuildDir 'engine.exe'
if (-not (Test-Path $engineSrc)) { throw "engine.exe not found at $engineSrc. Build the engine first." }

# 0. Migrate legacy startup mechanisms and stop known encoder instances FIRST.
$startup = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Startup'
$legacyInstallDir = Join-Path $env:LOCALAPPDATA 'Virtual AC3 Encoder'
$legacyVbs = Join-Path $startup 'VirtualAc3Encoder.vbs'
$legacyLnk = Join-Path $startup 'Virtual AC3 Encoder.lnk'
$ohlLnk = Join-Path $startup 'OHL Virtual AC3 Encoder.lnk'

Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
  Where-Object {
    ($_.Name -ieq 'engine.exe' -and (
      ($_.ExecutablePath -and (
        $_.ExecutablePath.StartsWith($InstallDir, [System.StringComparison]::OrdinalIgnoreCase) -or
        $_.ExecutablePath.StartsWith($legacyInstallDir, [System.StringComparison]::OrdinalIgnoreCase)
      )) -or
      ($_.CommandLine -and (
        $_.CommandLine -like '*virtual-ac3-encoder*' -or
        $_.CommandLine -like '*Virtual AC3 Encoder*'
      ))
    )) -or
    ($_.Name -ieq 'wscript.exe' -and $_.CommandLine -and (
      $_.CommandLine -like '*VirtualAc3Encoder*' -or
      $_.CommandLine -like '*Virtual AC3 Encoder*'
    ))
  } |
  ForEach-Object { try { $_ | Invoke-CimMethod -MethodName Terminate | Out-Null } catch {} }

foreach ($p in @($legacyVbs, $legacyLnk, $ohlLnk)) {
  if (Test-Path $p) { Remove-Item $p -Force }
}
Start-Sleep -Milliseconds 600

# 1. Stage engine.exe + FFmpeg DLLs (and the VC runtime, so it's self-contained) into a stable dir.
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item $engineSrc $InstallDir -Force
Copy-Item (Join-Path $BuildDir '*.dll') $InstallDir -Force
foreach ($d in 'VCRUNTIME140.dll','VCRUNTIME140_1.dll','MSVCP140.dll') {
  $p = Join-Path $env:WINDIR "System32\$d"
  if (Test-Path $p) { Copy-Item $p $InstallDir -Force }
}
$exePath = Join-Path $InstallDir 'engine.exe'
$logPath = Join-Path $InstallDir 'engine.log'
Write-Host "Staged engine + DLLs -> $InstallDir"

# 2. Config the engine reads on startup (next to the exe).
Set-Content -Path (Join-Path $InstallDir 'virtual-ac3-encoder.conf') -Encoding UTF8 -Value @(
  '# virtual-ac3-encoder configuration (edit, then restart the engine / log off+on)'
  "in=$In"
  "out=$Out"
  "bitrate=$Bitrate"
  "loopback=$([int][bool]$Loopback)"
  "tray=1"
)
Write-Host "Wrote config (in='$In', out='$Out', bitrate=$Bitrate, loopback=$([bool]$Loopback))"

# 3. Direct Startup shortcut to the persistent OHL engine. No WScript/supervisor layer.
New-Item -ItemType Directory -Force -Path $startup | Out-Null
$ws = New-Object -ComObject WScript.Shell
$startupShortcut = $ws.CreateShortcut($ohlLnk)
$startupShortcut.TargetPath = $exePath
$startupShortcut.Arguments = '--hidden --log "' + $logPath + '"'
$startupShortcut.WorkingDirectory = $InstallDir
$startupShortcut.Description = 'OHL Virtual AC3 Encoder - day-to-day engine'
$startupShortcut.IconLocation = $exePath + ',0'
$startupShortcut.Save()
Write-Host "Installed authoritative Startup shortcut -> $ohlLnk"

# 4. Start it now (don't wait for the next logon).
Get-CimInstance Win32_Process -Filter "Name='engine.exe'" |
  Where-Object { $_.CommandLine -like "*virtual-ac3-encoder*" } |
  ForEach-Object { $_ | Invoke-CimMethod -MethodName Terminate | Out-Null }
$daemonArgs = '--hidden --log "' + $logPath + '"'
Start-Process -FilePath $exePath -ArgumentList $daemonArgs -WorkingDirectory $InstallDir -WindowStyle Hidden
Start-Sleep -Seconds 3
$running = [bool](Get-CimInstance Win32_Process -Filter "Name='engine.exe'")
Write-Host "Started. engine running: $running"
Write-Host "Log: $logPath   |  Remove with: scripts\remove-autostart.ps1"
