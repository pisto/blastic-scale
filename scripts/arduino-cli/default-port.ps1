if (-not $env:BLASTIC_SCALE_PORT) {
    $BLASTIC_SCALE_PORT = arduino-cli board list --fqbn arduino:renesas_uno:unor4wifi |
        Select-String "arduino:renesas_uno:unor4wifi" |
        ForEach-Object { ($_ -split '\s+')[0] }
    if (-not $BLASTIC_SCALE_PORT) {
        Write-Error "Cannot detect a single arduino:renesas_uno:unor4wifi board. Define BLASTIC_SCALE_PORT manually."
        exit 1
    }
    $env:BLASTIC_SCALE_PORT = $BLASTIC_SCALE_PORT
}
Write-Output "$env:BLASTIC_SCALE_PORT"
