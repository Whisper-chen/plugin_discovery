#!/usr/bin/env bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Run every scenario and compare the transcripts.
#
#   test/run_diff.sh <build-dir> [scenario ...]
#
#   ours vs upstream     same loader-visible state, same exception types.
#                        Scenarios with a file in test/expected/ differ from upstream ON
#                        PURPOSE (reason on the first line of the file); there ours is
#                        compared with that recorded transcript instead.
#   ours vs deletable    loader-visible state must not depend on whether dlclose
#                        really unmaps the library (mapped=... is masked), and the
#                        deletable build must really unmap where expected.
#
# Needs a build configured with -DPLUGIN_LOADER_COMPARE_UPSTREAM=ON and the ROS
# environment sourced (upstream binaries need its libraries). Without the upstream
# binaries only the ours-vs-deletable checks run.
set -u

build=${1:?usage: run_diff.sh <build-dir> [scenario ...]}
shift
t="$build/test"
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
dirs=(ours deletable upstream)
have_upstream=0
[[ -x "$t/upstream/scenarios_upstream" ]] && have_upstream=1

if [[ $# -gt 0 ]]; then
  scenarios=("$@")
else
  mapfile -t scenarios < <("$t/ours/scenarios_ours" "$t/ours" --list)
fi

# Transcript + exit status (so a crash is a difference too).
run() {
  local flavor=$1 scenario=$2 variant=scenarios output rc
  [[ $scenario == linked_directly ]] && variant=scenarios_linked
  output=$(timeout 60 "$t/$flavor/${variant}_${flavor}" "$t/$flavor" "$scenario" 2>/dev/null)
  rc=$?
  printf '%s\n[exit %d]\n' "$output" "$rc"
}
mask() { sed -E 's/mapped=(true|false)/mapped=*/g'; }

pass=0; fail=0
report() {  # report <label> <scenario> <expected> <actual>
  if diff_out=$(diff <(printf '%s\n' "$3") <(printf '%s\n' "$4")); then
    printf 'PASS  %-34s %s\n' "$2" "$1"; pass=$((pass + 1))
  else
    printf 'DIFF  %-34s %s   (< expected  > actual)\n' "$2" "$1"; fail=$((fail + 1))
    printf '%s\n' "$diff_out" | sed 's/^/        /'
  fi
}

for sc in "${scenarios[@]}"; do
  ours=$(run ours "$sc")
  deletable=$(run deletable "$sc")
  report "ours vs deletable (mapped masked)" "$sc" "$(printf '%s\n' "$ours" | mask)" \
    "$(printf '%s\n' "$deletable" | mask)"
  golden="$here/expected/$sc.txt"
  if [[ -f $golden ]]; then
    report "ours vs recorded transcript (intentional deviation)" "$sc" \
      "$(grep -v '^# ' "$golden")" "$ours"
    printf '        note: %s\n' "$(head -n1 "$golden" | sed 's/^# //')"
  elif [[ $have_upstream -eq 1 ]]; then
    report "ours vs upstream" "$sc" "$(run upstream "$sc")" "$ours"
  fi
done

# The deletable build must really unmap the library.
check_unmapped() {  # check_unmapped <scenario> <fixed-string that must appear>
  if run deletable "$1" | grep -qF "$2"; then
    printf 'PASS  %-34s deletable really unmaps ("%s")\n' "$1" "$2"; pass=$((pass + 1))
  else
    printf 'FAIL  %-34s expected "%s"\n' "$1" "$2"; fail=$((fail + 1))
  fi
}
for sc in "${scenarios[@]}"; do
  case $sc in
    destructor_unloads) check_unmapped "$sc" "after destroy mapped=false" ;;
    multi_destructor)   check_unmapped "$sc" "after destroy mapped=false" ;;
    reload)             check_unmapped "$sc" "unloaded: loader=false anybody=false mapped=false" ;;
  esac
done

printf '\n%d passed, %d differ\n' "$pass" "$fail"
[[ $fail -eq 0 ]]
