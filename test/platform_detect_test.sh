#!/usr/bin/env bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Tests of platform-detect with a fake machine, os-release and directories.
#   test/platform_detect_test.sh <build-dir>
set -u
BIN="${1:?usage: platform_detect_test.sh <build-dir>}/platform-detect"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }

mkdir -p "$T/cand"
touch "$T/cand/QCS8275-Ubuntu-camera.config.yaml" "$T/cand/QCS8275-QCLinux-camera.config.yaml" \
      "$T/cand/QCS9075-Ubuntu-camera.config.yaml" "$T/cand/qcs8275-ubuntu-imu.config.yaml" \
      "$T/cand/QCS8275-Ubuntu-.config.yaml" "$T/cand/notes.txt"
echo "QCS8275" > "$T/machine"
printf 'NAME="Ubuntu"\nID=ubuntu\nVERSION_ID="24.04"\n' > "$T/ubuntu"
printf 'ID=qcom-robotics-ros2-jazzy\nNAME="Qualcomm Linux Robotics Reference Distro with ROS"\n' > "$T/qcl"
printf 'ID=debian\nNAME="Debian GNU/Linux"\n' > "$T/debian"

run() {  # run <os-release> [extra args...]
  local os="$1"; shift
  "$BIN" --candidates "$T/cand" --platform-dir "$T/plat" --stamp "$T/stamp" \
         --machine-file "$T/machine" --os-release "$os" "$@"
}

run "$T/ubuntu" >/dev/null 2>&1
check "Ubuntu: camera linked to the matching candidate (case-insensitive)" \
  '[ "$(readlink "$T/plat/camera.config.yaml")" = "$T/cand/QCS8275-Ubuntu-camera.config.yaml" ]'
check "Ubuntu: imu linked (candidate name in lower case)" '[ -L "$T/plat/imu.config.yaml" ]'
check "Ubuntu: nothing else linked (other machine / OS / empty domain)" \
  '[ "$(ls "$T/plat" | wc -l)" = 2 ]'
check "stamp written" '[ -f "$T/stamp" ]'

rm "$T/plat/imu.config.yaml"
run "$T/qcl" >/dev/null 2>&1
check "second run with the stamp does nothing (hand edits are kept)" \
  '[ ! -e "$T/plat/imu.config.yaml" ]'

run "$T/qcl" --force >/dev/null 2>&1
check "--force on Qualcomm Linux: camera now points to the QCLinux candidate" \
  '[ "$(readlink "$T/plat/camera.config.yaml")" = "$T/cand/QCS8275-QCLinux-camera.config.yaml" ]'
check "--force drops links made earlier that no longer match (imu)" '[ ! -e "$T/plat/imu.config.yaml" ]'

rm -rf "$T/plat" "$T/stamp"
out="$(run "$T/debian" 2>&1)"; rc=$?
check "unknown OS: exit 0, WARN, no link" \
  '[ $rc = 0 ] && echo "$out" | grep -q "WARN: unknown OS" && [ ! -e "$T/plat" ]'
check "unknown OS: stamp still written (it ran)" '[ -f "$T/stamp" ]'

rm -rf "$T/stamp"; echo "QCS0000" > "$T/machine"
out="$(run "$T/ubuntu" 2>&1)"; rc=$?
check "no candidate for the machine: exit 0 and WARN about unconfigured domains" \
  '[ $rc = 0 ] && echo "$out" | grep -q "stay unconfigured"'

rm -rf "$T/stamp" "$T/plat"; echo "QCS8275" > "$T/machine"
mkdir -p "$T/plat"; echo "domains: {}" > "$T/plat/camera.config.yaml"
run "$T/ubuntu" --force >/dev/null 2>&1
check "an existing regular file in platform.d is never replaced" \
  '[ -f "$T/plat/camera.config.yaml" ] && [ ! -L "$T/plat/camera.config.yaml" ]'

rm -rf "$T/stamp" "$T/plat"
run "$T/ubuntu" --dry-run >/dev/null 2>&1
check "--dry-run changes nothing" '[ ! -e "$T/plat" ] && [ ! -e "$T/stamp" ]'

exit $fail
