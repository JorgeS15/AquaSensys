#!/usr/bin/env bash
# Host tests: firmware logic that needs no ESP32, built with the system g++.
# Run from anywhere: bash tests/host/run.sh. Exits non-zero if any test fails.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
code="$here/../../code"
build="$(mktemp -d)"
trap 'rm -r "$build"' EXIT

# The firmware sources include "config_manager.h" and "time_log.h", which a
# quoted include looks for next to the source first: copy the sources next to
# the stubs so the stubs are the ones found.
cp "$here"/stubs/*.h "$here"/check.h "$build"/
cp "$code"/alarms.h "$code"/alarms.cpp "$code"/flow_math.h "$build"/

flags=(-std=c++17 -Wall -Wextra -Werror -O1 -I"$build")
status=0
g++ "${flags[@]}" -o "$build/test_alarms" "$here/test_alarms.cpp" "$build/alarms.cpp"
g++ "${flags[@]}" -o "$build/test_flow"   "$here/test_flow.cpp"
"$build/test_alarms" || status=1
"$build/test_flow"   || status=1
exit $status
