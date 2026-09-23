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

# Known legacy install/startup locations from the original upstream installer and older OHL
# development builds. The day-to-day installer migrates these to one authoritative startup entry.
$legacyInstallDir = Join-Path $localAppData 'Virtual AC3 Encoder'
$legacyStartupVbs = Join-Path $startup 'VirtualAc3Encoder.vbs'
$legacyStartupLnk = Join-Path $startup 'Virtual AC3 Encoder.lnk'
$ohlStartupLnk = Join-Path $startup 'OHL Virtual AC3 Encoder.lnk'

$engineSrc = Join-Path $SourceDir 'engine.exe'
if (-not (Test-Path $engineSrc)) {
  throw "engine.exe not found next to this script: $engineSrc"
}

$startMenuDir = Join-Path $programsDir 'OHL Virtual AC3 Encoder'
$shortcutPath = Join-Path $startMenuDir 'Audio Mode Switcher.lnk'

Write-Host "Source folder     -> $SourceDir"
Write-Host "Install folder    -> $InstallDir"
Write-Host "Startup folder    -> $startup"
Write-Host "Start Menu folder -> $programsDir"
Write-Host ''
Write-Host 'Migrating legacy autostart and stopping old encoder processes...'

$knownRoots = @($InstallDir, $legacyInstallDir) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
  Where-Object {
    $p = $_
    $knownEngine = $false
    if ($p.Name -ieq 'engine.exe') {
      foreach ($root in $knownRoots) {
        if ($p.ExecutablePath -and $p.ExecutablePath.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
          $knownEngine = $true
          break
        }
      }
      if (-not $knownEngine -and $p.CommandLine) {
        $knownEngine = (
          $p.CommandLine -like '*virtual-ac3-encoder*' -or
          $p.CommandLine -like '*Virtual AC3 Encoder*'
        )
      }
    }

    $knownWscript = (
      $p.Name -ieq 'wscript.exe' -and $p.CommandLine -and (
        $p.CommandLine -like '*VirtualAc3Encoder*' -or
        $p.CommandLine -like '*Virtual AC3 Encoder*' -or
        $p.CommandLine -like '*virtual-ac3-encoder*'
      )
    )

    $knownEngine -or $knownWscript
  } |
  ForEach-Object {
    try {
      Write-Host "  stopping $($_.Name) PID $($_.ProcessId)"
      $_ | Invoke-CimMethod -MethodName Terminate | Out-Null
    } catch {}
  }

Start-Sleep -Milliseconds 700

# Remove every known legacy Startup mechanism before installing the one authoritative OHL link.
foreach ($legacyPath in @($legacyStartupVbs, $legacyStartupLnk, $ohlStartupLnk)) {
  if (Test-Path $legacyPath) {
    Remove-Item $legacyPath -Force
    Write-Host "  removed startup entry: $legacyPath"
  }
}

# Older development revisions also used a Scheduled Task. Remove it when possible.
foreach ($taskName in @('VirtualAc3Encoder', 'Virtual AC3 Encoder')) {
  try {
    $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($task) {
      Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
      Write-Host "  removed scheduled task: $taskName"
    }
  } catch {
    Write-Warning "Could not remove legacy scheduled task '$taskName'. If it still exists, remove it from Task Scheduler."
  }
}

# Best-effort cleanup for old HKCU Run entries from experimental builds.
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
foreach ($valueName in @('VirtualAc3Encoder', 'Virtual AC3 Encoder', 'OHL Virtual AC3 Encoder')) {
  try {
    $value = Get-ItemProperty -Path $runKey -Name $valueName -ErrorAction SilentlyContinue
    if ($null -ne $value) {
      Remove-ItemProperty -Path $runKey -Name $valueName -ErrorAction Stop
      Write-Host "  removed HKCU Run entry: $valueName"
    }
  } catch {}
}

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
New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null
$ws = New-Object -ComObject WScript.Shell

# One authoritative logon path: Windows Startup launches THIS installed OHL engine directly.
# No supervisor, no old fixed-5.1 executable, no second install tree.
$startupShortcut = $ws.CreateShortcut($ohlStartupLnk)
$startupShortcut.TargetPath = $exePath
$startupShortcut.Arguments = '--hidden --log "' + $logPath + '"'
$startupShortcut.WorkingDirectory = $InstallDir
$startupShortcut.Description = 'OHL Virtual AC3 Encoder - day-to-day engine'
$startupShortcut.IconLocation = $exePath + ',0'
$startupShortcut.Save()

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
Write-Host "Authoritative startup -> $ohlStartupLnk"
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
Write-Host 'Starting background engine directly...'

# Launch the real daemon directly. The Startup VBS is now only a one-shot logon launcher,
# so installation no longer depends on WScript successfully supervising the process.
$daemonArgs = '--hidden --log "' + $logPath + '"'
$daemon = Start-Process -FilePath $exePath -ArgumentList $daemonArgs -WorkingDirectory $InstallDir -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 2

if ($daemon.HasExited) {
  $exitCode = $daemon.ExitCode
  $hex = ('0x{0:X8}' -f ([uint32]$exitCode))
  if (Test-Path $logPath) {
    Write-Host ''
    Write-Host '----- engine.log -----'
    Get-Content $logPath -Tail 120 | ForEach-Object { Write-Host $_ }
    Write-Host '----------------------'
    throw "engine.exe exited during daemon startup. Exit code: $exitCode ($hex). The last engine.log lines are above."
  }
  throw "engine.exe exited during daemon startup with exit code $exitCode ($hex), and no engine.log was created."
}

$running = @(Get-CimInstance Win32_Process -Filter "Name='engine.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.ExecutablePath -like "$InstallDir*" }).Count -gt 0

if (-not $running) {
  throw "engine.exe was launched but could not be found in the process table after 2 seconds."
}

Write-Host ''
Write-Host 'OHL Virtual AC3 Encoder is running.'
Write-Host 'Use the notification-area icon to switch SURROUND / GUITAR modes.'
