# Installs/updates the CI portable build as the user's day-to-day OHL Virtual AC3 Encoder.
# No admin rights required. Existing virtual-ac3-encoder.conf is preserved.
[CmdletBinding()]
param(
  [string]$SourceDir = $PSScriptRoot,
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'virtual-ac3-encoder')
)

$ErrorActionPreference = 'Stop'

$engineSrc = Join-Path $SourceDir 'engine.exe'
if (-not (Test-Path $engineSrc)) {
  throw "engine.exe not found next to this script: $engineSrc"
}

$startup = [Environment]::GetFolderPath('Startup')
$supervisorPath = Join-Path $startup 'VirtualAc3Encoder.vbs'
$startMenuDir = Join-Path ([Environment]::GetFolderPath('Programs')) 'OHL Virtual AC3 Encoder'
$shortcutPath = Join-Path $startMenuDir 'Audio Mode Switcher.lnk'

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

Set-Content -Path $supervisorPath -Encoding ASCII -Value @(
  "' OHL Virtual AC3 Encoder supervisor."
  'Set sh = CreateObject("WScript.Shell")'
  'q = Chr(34)'
  "appPath = ""$exePath"""
  "logFile = ""$logPath"""
  'Do'
  '  sh.Run q & appPath & q & " --hidden --log " & q & logFile & q, 0, True'
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
Write-Host 'Starting background engine...'

Start-Process -FilePath "$env:WINDIR\System32\wscript.exe" -ArgumentList ('"' + $supervisorPath + '"') -WindowStyle Hidden
Start-Sleep -Seconds 2

$running = @(Get-CimInstance Win32_Process -Filter "Name='engine.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.ExecutablePath -like "$InstallDir*" }).Count -gt 0

if (-not $running) {
  throw "engine.exe did not stay running. Check $logPath"
}

Write-Host ''
Write-Host 'OHL Virtual AC3 Encoder is running.'
Write-Host 'Use the notification-area icon to switch SURROUND / GUITAR modes.'
