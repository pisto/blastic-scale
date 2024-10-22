#!/bin/bash

set -euo pipefail

arduino-cli core install arduino:renesas_uno@1.2.2
arduino-cli lib install ArduinoGraphics@1.1.3 ArduinoHttpClient@0.6.1 R4_Touch@1.1.0
arduino-cli compile -v --fqbn arduino:renesas_uno:unor4wifi --build-path .arduino-cli-build/ --build-property "build.extra_flags=-I$(realpath .)/include -DBLASTIC_MONITOR_SPEED=115200 $(python git_rev_macro.py | xargs) -DconfigUSE_TIME_SLICING=1 -DconfigUSE_TICKLESS_IDLE=0 -DconfigUSE_IDLE_HOOK=1 -DconfigUSE_MUTEXES=1 -DconfigUSE_RECURSIVE_MUTEXES=1 -DconfigUSE_TIMERS=0 -DconfigSUPPORT_STATIC_ALLOCATION=1 -DconfigUSE_MALLOC_FAILED_HOOK=1 -DconfigCHECK_FOR_STACK_OVERFLOW=2 -fstack-usage -g1" --build-property 'build.extra_ldflags=-Wl,--wrap=__malloc_lock -Wl,--wrap=__malloc_unlock -Wl,--wrap=_malloc_r -Wl,--cref -Wl,--wrap=__assert_func' "${@}" .
