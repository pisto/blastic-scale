#!/bin/bash

. ./scripts/arduino-cli/default-port.sh

set -euo pipefail

arduino-cli monitor --fqbn arduino:renesas_uno:unor4wifi -p "$BLASTIC_SCALE_PORT" --config baudrate=115200 "${@}"
