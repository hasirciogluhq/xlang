#!/bin/bash
# End-to-end tests: build each program with the compiler and run it.
#   test/e2e/run.sh [path/to/xlang]
# Runs natively; when Docker is available, also on Linux aarch64 and x86_64 (cross-built).
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
xlang=${1:-$root/build/xlang}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass=0; fail=0

# name | expected exit code | expected stdout (a `grep -F` substring) | needs raw OS threads
tests=(
  "language|0||0"
  "stdlib|0|499500|0"
  "fsjson|0||0"
  "panics|134|before|0"
  "threads|0|40000|1"
  "sched|0|20100|0"
  "sched_io|0|5|0"
)

host_os=$(uname -s)

run_one() {   # target-label  binary  expected-exit  expected-text  runner...
  local label=$1 bin=$2 want=$3 text=$4; shift 4
  local out code
  out=$("$@" "$bin" 2>&1); code=$?
  if [ "$code" != "$want" ]; then
    echo "FAIL [$label] exit $code (want $want)"; echo "$out" | head -5; fail=$((fail+1)); return
  fi
  if [ -n "$text" ] && ! grep -qF "$text" <<<"$out"; then
    echo "FAIL [$label] output lacks '$text'"; echo "$out" | head -5; fail=$((fail+1)); return
  fi
  echo "ok   [$label]"; pass=$((pass+1))
}

docker_run() {  # platform binary
  local plat=$1 bin=$2 cid
  cid=$(docker run -d --platform "linux/$plat" -v "$(dirname "$bin")":/w alpine:3 "/w/$(basename "$bin")") || return 125
  for _ in $(seq 1 60); do
    [ "$(docker inspect -f '{{.State.Running}}' "$cid")" = "false" ] && break
    sleep 0.5
  done
  if [ "$(docker inspect -f '{{.State.Running}}' "$cid")" = "true" ]; then
    docker kill "$cid" >/dev/null; docker rm -f "$cid" >/dev/null; echo "timeout"; return 124
  fi
  docker logs "$cid" 2>&1
  local code; code=$(docker inspect -f '{{.State.ExitCode}}' "$cid")
  docker rm -f "$cid" >/dev/null
  return "$code"
}

for t in "${tests[@]}"; do
  IFS='|' read -r name want text threads <<<"$t"
  src=$here/$name.xl
  # native (macOS has no raw thread creation yet: the scheduler falls back to one thread)
  if [ "$threads" = 1 ] && [ "$host_os" = Darwin ]; then
    echo "skip [$host_os/$name] raw OS threads are not available on macOS"
  elif "$xlang" build "$src" -o "$work/$name" 2>"$work/$name.err"; then
    run_one "$host_os/$name" "$work/$name" "$want" "$text" env
  else
    echo "FAIL [$host_os/$name] build"; head -8 "$work/$name.err"; fail=$((fail+1))
  fi
  # Linux through Docker
  if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    for spec in "arm64:aarch64-unknown-linux-gnu" "amd64:x86_64-unknown-linux-gnu"; do
      plat=${spec%%:*}; triple=${spec#*:}
      if "$xlang" build "$src" --target "$triple" -o "$work/$name.$plat" 2>"$work/$name.$plat.err"; then
        out=$(docker_run "$plat" "$work/$name.$plat"); code=$?
        if [ "$code" != "$want" ] || { [ -n "$text" ] && ! grep -qF "$text" <<<"$out"; }; then
          echo "FAIL [linux-$plat/$name] exit $code"; echo "$out" | head -5; fail=$((fail+1))
        else
          echo "ok   [linux-$plat/$name]"; pass=$((pass+1))
        fi
      else
        echo "FAIL [linux-$plat/$name] build"; head -8 "$work/$name.$plat.err"; fail=$((fail+1))
      fi
    done
  fi
done

echo "e2e: $pass passed, $fail failed"
[ "$fail" = 0 ]
