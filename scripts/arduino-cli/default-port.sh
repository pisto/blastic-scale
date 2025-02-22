#!/bin/bash

set -uo pipefail

if [[ -z "${BLASTIC_SCALE_PORT:-}" ]]; then
  BLASTIC_SCALE_PORT="$(arduino-cli board list --fqbn arduino:renesas_uno:unor4wifi | grep arduino:renesas_uno:unor4wifi | cut -f 1 -d ' ')"
  if [[ $? != 0 ]]; then
    unset BLASTIC_SCALE_PORT
    echo cannot detect a single arduino:renesas_uno:unor4wifi board, define BLASTIC_SCALE_PORT manually 1>&2
    exit 1
  fi
fi
echo "$BLASTIC_SCALE_PORT"
export BLASTIC_SCALE_PORT
