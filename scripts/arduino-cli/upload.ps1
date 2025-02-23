$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

. .\scripts\arduino-cli\default-port.ps1

if (-not $env:BLASTIC_SCALE_PORT) {
    Write-Error "BLASTIC_SCALE_PORT is not set. Please run default-port.ps1 or set it manually."
    exit 1
}
$firmware = if ($args.Count -gt 0) { $args[0] } else { ".\.arduino-cli-build\blastic-scale.ino.bin" }
if ($args.Count -gt 0) {
    $args = $args[1..($args.Count - 1)]
}

arduino-cli upload --fqbn arduino:renesas_uno:unor4wifi -i "$firmware" -p "$env:BLASTIC_SCALE_PORT" $args
