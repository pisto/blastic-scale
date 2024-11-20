#!/bin/bash

. ./scripts/arduino-cli/default-port.sh

set -euo pipefail

firmware="${1:-.arduino-cli-build/blastic-scale.ino.bin}"
[[ $# > 0 ]] && shift 1 || true

arduino-cli upload --fqbn arduino:renesas_uno:unor4wifi -i "$firmware" -p "$BLASTIC_SCALE_PORT" "${@}"
