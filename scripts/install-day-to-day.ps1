# Installs/updates the CI portable build as the user's day-to-day OHL Virtual AC3 Encoder.
# No admin rights required. Existing virtual-ac3-encoder.conf is preserved.
[CmdletBinding()]
param(
  [string]$SourceDir = '',
  [string]$InstallDir = ''
)

$ErrorActionPreference = 'Stop'

# Resolve paths defensively. Some Windows/PowerShell installations can return an empty string
# from Environment.GetFolderPath for Start Menu folders, and Join-Path refuses an empty -Path.
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
  $SourceDir = $PSScriptRoot
}
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
  $SourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
}
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
  throw 'Could not determine the extracted build folder.'
}

$localAppData = $env:LOCALAPPDATA
if ([string]::IsNullOrWhiteSpace($localAppData)) {
  $localAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
}
if ([string]::IsNullOrWhiteSpace($localAppData)) {
  throw 'Could not resolve LOCALAPPDATA.'
}

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
  $InstallDir = Join-Path $localAppData 'virtual-ac3-encoder'
}

$roamingAppData = $env:APPDATA
if ([string]::IsNullOrWhiteSpace($roamingAppData)) {
  $roamingAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::ApplicationData)
}
if ([string]::IsNullOrWhiteSpace($roamingAppData)) {
  throw 'Could not resolve APPDATA.'
}

$startMenuRoot = Join-Path $roamingAppData 'Microsoft\Windows\Start Menu'
$programsDir = Join-Path $startMenuRoot 'Programs'
$startup = Join-Path $programsDir 'Startup'

$engineSrc = Join-Path $SourceDir 'engine.exe'
if (-not (Test-Path $engineSrc)) {
  throw "engine.exe not found next to this script: $engineSrc"
}

$supervisorPath = Join-Path $startup 'VirtualAc3Encoder.vbs'
$startMenuDir = Join-Path $programsDir 'OHL Virtual AC3 Encoder'
$shortcutPath = Join-Path $startMenuDir 'Audio Mode Switcher.lnk'

Write-Host "Source folder     -> $SourceDir"
Write-Host "Install folder    -> $InstallDir"
Write-Host "Startup folder    -> $startup"
Write-Host "Start Menu folder -> $programsDir"
Write-Host ''
Write-Host 'Stopping current OHL / virtual-ac3-encoder processes...'
Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
  Where-Object {
    ($_.Name -ieq 'engine.exe' -and (
      $_.ExecutablePath -like "$InstallDir*" -or
      $_.CommandLine -like '*virtual-ac3-encoder*'
    )) -or
    ($_.Name -ieq 'wscript.exe' -and (
      $_.CommandLine -like '*VirtualAc3Encoder*' -or
      $_.CommandLine -like '*virtual-ac3-encoder*'
    ))
  } |
  ForEach-Object {
    try { $_ | Invoke-CimMethod -MethodName Terminate | Out-Null } catch {}
  }

Start-Sleep -Milliseconds 700

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item $engineSrc $InstallDir -Force
Get-ChildItem -Path $SourceDir -Filter '*.dll' -File | Copy-Item -Destination $InstallDir -Force

$configDst = Join-Path $InstallDir 'virtual-ac3-encoder.conf'
$configSrc = Join-Path $SourceDir 'virtual-ac3-encoder.conf'
if (-not (Test-Path $configDst)) {
  if (Test-Path $configSrc) {
    Copy-Item $configSrc $configDst
  } else {
    Set-Content -Path $configDst -Encoding UTF8 -Value @(
      '# OHL Virtual AC3 Encoder configuration'
      'in=CABLE Output'
      'out=Realtek Digital Output'
      'bitrate=640000'
      'loopback=0'
      'layout=auto'
      'auto_threshold_db=-60'
      'auto_hold_ms=2000'
      'tray=1'
    )
  }
}

$configText = Get-Content $configDst -Raw
if ($configText -notmatch '(?m)^\s*tray\s*=') {
  Add-Content -Path $configDst -Encoding UTF8 -Value @(
    ''
    '# Windows notification-area controller'
    'tray=1'
  )
}

$exePath = Join-Path $InstallDir 'engine.exe'
$logPath = Join-Path $InstallDir 'engine.log'

New-Item -ItemType Directory -Force -Path $startup | Out-Null
Set-Content -Path $supervisorPath -Encoding ASCII -Value @(
  "' OHL Virtual AC3 Encoder supervisor."
  'Set sh = CreateObject("WScript.Shell")'
  'q = Chr(34)'
  "appPath = ""$exePath"""
  "logFile = ""$logPath"""
  'Do'
  '  rc = sh.Run(q & appPath & q & " --hidden --log " & q & logFile & q, 0, True)'
  '  If rc = 10 Then Exit Do'
  '  WScript.Sleep 5000'
  'Loop'
)

New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null
$ws = New-Object -ComObject WScript.Shell
$shortcut = $ws.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $exePath
$shortcut.Arguments = '--switcher'
$shortcut.WorkingDirectory = $InstallDir
$shortcut.Description = 'OHL Virtual AC3 Encoder - Surround / Guitar mode switcher'
$shortcut.Save()

Write-Host "Installed engine -> $InstallDir"
Write-Host "Preserved config  -> $configDst"
Write-Host "Tray control      -> enabled"
Write-Host "Mode shortcut     -> $shortcutPath"
Write-Host ''
Write-Host 'Preflight: launching the staged engine directly...'

# Run a tiny foreground preflight before hiding it behind wscript. If Windows cannot load the EXE
# at all (for example a missing VC runtime DLL), main() never runs and engine.log cannot exist.
$preflight = Start-Process -FilePath $exePath -ArgumentList '--version' -WorkingDirectory $InstallDir -Wait -PassThru
if ($preflight.ExitCode -ne 0) {
  $hex = ('0x{0:X8}' -f ([uint32]$preflight.ExitCode))
  throw "engine.exe failed before daemon startup. Exit code: $($preflight.ExitCode) ($hex). This is usually a loader/dependency problem; the build should include its MSVC runtime DLLs."
}

Write-Host 'Preflight passed.'
Write-Host 'Starting background engine...'

Start-Process -FilePath "$env:WINDIR\System32\wscript.exe" -ArgumentList ('"' + $supervisorPath + '"') -WindowStyle Hidden
Start-Sleep -Seconds 2

$running = @(Get-CimInstance Win32_Process -Filter "Name='engine.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.ExecutablePath -like "$InstallDir*" }).Count -gt 0

if (-not $running) {
  if (Test-Path $logPath) {
    Write-Host ''
    Write-Host '----- engine.log -----'
    Get-Content $logPath -Tail 80 | ForEach-Object { Write-Host $_ }
    Write-Host '----------------------'
    throw "engine.exe started but did not stay running. The last engine.log lines are above."
  }
  throw "engine.exe did not stay running and no engine.log was created. The process likely failed before main() or the supervisor could not launch it."
}

Write-Host ''
Write-Host 'OHL Virtual AC3 Encoder is running.'
Write-Host 'Use the notification-area icon to switch SURROUND / GUITAR modes.'
