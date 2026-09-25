# Stop known OHL / legacy Virtual AC3 Encoder processes. Safe to run anytime.
$local = $env:LOCALAPPDATA
$current = Join-Path $local 'virtual-ac3-encoder'
$legacy = Join-Path $local 'Virtual AC3 Encoder'

Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
  Where-Object {
    ($_.Name -ieq 'engine.exe' -and (
      ($_.ExecutablePath -and (
        $_.ExecutablePath.StartsWith($current, [System.StringComparison]::OrdinalIgnoreCase) -or
        $_.ExecutablePath.StartsWith($legacy, [System.StringComparison]::OrdinalIgnoreCase)
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
