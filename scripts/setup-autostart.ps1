<#
.SYNOPSIS
  "Set and forget": installs the engine to a stable per-user location, writes a config file,
  and autostarts it hidden at logon via a one-shot Startup-folder launcher.

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

# 0. Stop any running instance (engine + Startup supervisor) FIRST, so we can overwrite the
#    staged exe/DLLs on a re-run/update (otherwise the copy fails: file in use).
Get-CimInstance Win32_Process -Filter "Name='engine.exe' OR Name='wscript.exe'" |
  Where-Object { $_.CommandLine -like "*virtual-ac3-encoder*" -or $_.CommandLine -like "*VirtualAc3Encoder*" } |
  ForEach-Object { $_ | Invoke-CimMethod -MethodName Terminate | Out-Null }
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

# 3. One-shot VBScript in the Startup folder: starts the persistent engine hidden at logon.
#    Runtime SURROUND/GUITAR switching happens inside engine.exe; no external watchdog is needed.
$startup = [Environment]::GetFolderPath('Startup')
$vbsPath = Join-Path $startup 'VirtualAc3Encoder.vbs'
Set-Content -Path $vbsPath -Encoding ASCII -Value @(
  "' Virtual AC3 Encoder autostart launcher."
  'Set sh = CreateObject("WScript.Shell")'
  'q = Chr(34)'
  "appPath = ""$exePath"""
  "logFile = ""$logPath"""
  'sh.Run q & appPath & q & " --hidden --log " & q & logFile & q, 0, False'
)
Write-Host "Installed Startup launcher -> $vbsPath"

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
