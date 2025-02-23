$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

. .\scripts\arduino-cli\default-port.ps1

# Ensure BLASTIC_SCALE_PORT is set
if (-not $env:BLASTIC_SCALE_PORT) {
    Write-Error "BLASTIC_SCALE_PORT is not set. Please run default-port.ps1 or set it manually."
    exit

arduino-cli monitor --fqbn arduino:renesas_uno:unor4wifi -p "$env:BLASTIC_SCALE_PORT" --config baudrate=115200 $args
