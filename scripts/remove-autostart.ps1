# Removes every known OHL / legacy Virtual AC3 Encoder autostart mechanism and stops the engines.
[CmdletBinding()]
param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'virtual-ac3-encoder'),
  [switch]$DeleteInstall
)
$ErrorActionPreference = 'Continue'

$startup = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Startup'
$legacyInstallDir = Join-Path $env:LOCALAPPDATA 'Virtual AC3 Encoder'

foreach ($p in @(
  (Join-Path $startup 'VirtualAc3Encoder.vbs'),
  (Join-Path $startup 'Virtual AC3 Encoder.lnk'),
  (Join-Path $startup 'OHL Virtual AC3 Encoder.lnk')
)) {
  if (Test-Path $p) {
    Remove-Item $p -Force
    Write-Host "Removed startup entry: $p"
  }
}

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
  ForEach-Object {
    try {
      $_ | Invoke-CimMethod -MethodName Terminate | Out-Null
      Write-Host "Stopped $($_.Name) PID $($_.ProcessId)"
    } catch {}
  }

foreach ($taskName in @('VirtualAc3Encoder', 'Virtual AC3 Encoder')) {
  try {
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
      Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
      Write-Host "Removed scheduled task: $taskName"
    }
  } catch {
    Write-Warning "Could not remove scheduled task '$taskName'."
  }
}

$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
foreach ($valueName in @('VirtualAc3Encoder', 'Virtual AC3 Encoder', 'OHL Virtual AC3 Encoder')) {
  try {
    if ($null -ne (Get-ItemProperty -Path $runKey -Name $valueName -ErrorAction SilentlyContinue)) {
      Remove-ItemProperty -Path $runKey -Name $valueName -ErrorAction Stop
      Write-Host "Removed HKCU Run entry: $valueName"
    }
  } catch {}
}

if ($DeleteInstall -and (Test-Path $InstallDir)) {
  Remove-Item $InstallDir -Recurse -Force
  Write-Host "Deleted $InstallDir."
}

Write-Host 'Done.'
