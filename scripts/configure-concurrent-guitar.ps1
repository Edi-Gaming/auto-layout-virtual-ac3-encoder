# Enable simultaneous system audio + VB-CABLE guitar capture in an existing
# installed OHL, without touching the optical endpoint or overwriting other
# music tuning. Creates a one-time backup that the disable action restores.
[CmdletBinding()]
param(
  [switch]$Undo,
  [string]$MusicEndpoint = 'Speakers',
  [string]$GuitarEndpoint = 'CABLE Output'
)
$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:LOCALAPPDATA 'virtual-ac3-encoder'
$engine = Join-Path $dir 'engine.exe'
$config = Join-Path $dir 'virtual-ac3-encoder.conf'
$backup = "$config.before-concurrent-guitar.bak"
if (-not (Test-Path $engine) -or -not (Test-Path $config)) {
  throw "Install OHL using INSTALL-DAY-TO-DAY.cmd before running this setup. Expected $dir"
}

if ($Undo) {
  if (-not (Test-Path $backup)) {
    throw "No concurrent-guitar backup found at $backup"
  }
  Copy-Item -LiteralPath $backup -Destination $config -Force
  Write-Host 'Restored your original OHL audio-source configuration.' -ForegroundColor Green
} else {
  # Query endpoint names before touching the live configuration.
  $found = (& $engine --list 2>&1 | Out-String)
  if ($LASTEXITCODE -ne 0) { throw 'OHL device enumeration failed.' }
  if ($found.IndexOf($MusicEndpoint, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw "Cannot find Windows music endpoint '$MusicEndpoint'. Run engine.exe --list and pass -MusicEndpoint 'exact substring'."
  }
  if ($found.IndexOf($GuitarEndpoint, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw "Cannot find guitar endpoint '$GuitarEndpoint'. VB-CABLE may not be installed."
  }
  if (-not (Test-Path $backup)) {
    Copy-Item -LiteralPath $config -Destination $backup
    Write-Host "Saved original configuration: $backup"
  }

  # Remove old values for only the keys below. Write one unambiguous value per
  # key; never alter out/out_id/out_spdif or user's music spatializer settings.
  $updates = [ordered]@{
    in = $MusicEndpoint
    in_id = ''
    loopback = 'true'
    guitar_in = $GuitarEndpoint
    guitar_in_id = ''
    guitar_loopback = 'false'
    guitar_gain = '0.50'
    layout = 'auto'
    stereo_processing = 'music'
  }
  $keyNames = @($updates.Keys | ForEach-Object { [regex]::Escape([string]$_) })
  $pattern = '^\s*(?:' + ($keyNames -join '|') + ')\s*='
  $lines = @(Get-Content -LiteralPath $config | Where-Object { $_ -notmatch $pattern })
  $lines += ''
  $lines += '# Concurrent OHL music + AmpliTube guitar (dual capture)'
  foreach ($k in $updates.Keys) {
    $lines += "$k=$($updates[$k])"
  }
  Set-Content -LiteralPath $config -Value $lines -Encoding UTF8
  Write-Host 'Concurrent guitar bus configured.' -ForegroundColor Green
  Write-Host "Music:   $MusicEndpoint (Windows render loopback)"
  Write-Host "Guitar:  $GuitarEndpoint (VB-CABLE capture)"
}
# The background engine rebuilds its graph without reinstalling or rebooting.
& $engine --mode reload
if ($LASTEXITCODE -ne 0) {
  Write-Warning 'Configuration was updated, but OHL is not running. Start OHL or rerun the installer.'
}
Write-Host ''
Write-Host 'Set Windows default sound to your music endpoint, not CABLE Input.'
Write-Host 'Configure AmpliTube / ASIO4ALL output to CABLE Input, never Realtek S/PDIF.'
Write-Host 'Query the live bus with: engine.exe --mode "guitar bus"'
Write-Host 'Read CONCURRENT-GUITAR-SETUP.txt for format/latency limitations.'
