#!/bin/bash
# Shared helpers for test/e2e/run.sh and test/e2e/packages.sh: a counting result sink and a parallel job
# pool. Sourced, never executed.
#
# The pool exists because a full run builds and starts every program twice per Linux architecture and once
# per Windows target; those legs are independent, so they run concurrently and the wall time is bounded by
# the slowest leg instead of the sum. Counter updates stay serialised by a directory-rename lock, so no
# `++` is lost between workers (bash arithmetic on a shared variable is not atomic).

XLANG_E2E_JOBS=${XLANG_E2E_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
[ "$XLANG_E2E_JOBS" -ge 1 ] 2>/dev/null || XLANG_E2E_JOBS=1
pool_dir=

## Opens the result sink and the job pool. Creates the counter files and the lock directory under `$1`.
poolInit() {
    pool_dir=$1
    mkdir -p "$pool_dir/lock"
    printf '0' >"$pool_dir/pass"
    printf '0' >"$pool_dir/fail"
    printf '0' >"$pool_dir/skip"
}

## Serialises one counter update. Usage: countAdd pass|fail|skip
countAdd() {
    local key=$1 got
    until mkdir "$pool_dir/lock/$key" 2>/dev/null; do sleep 0.01; done
    got=$(( $(cat "$pool_dir/$key") + 1 ))
    printf '%s' "$got" >"$pool_dir/$key"
    rmdir "$pool_dir/lock/$key"
}

## Copies the counters into the caller's `pass`, `fail` and `skip`.
countLoad() {
    pass=$(cat "$pool_dir/pass")
    fail=$(cat "$pool_dir/fail")
    skip=$(cat "$pool_dir/skip")
}

## Runs one job function in the background, at most XLANG_E2E_JOBS at a time, and waits for all of them.
## Usage: poolRun jobName arg...   (the job function receives the arguments and calls countAdd itself)
poolRun() {
    local job=$1; shift
    while [ "$(jobs -rp | wc -l)" -ge "$XLANG_E2E_JOBS" ]; do wait -n 2>/dev/null || sleep 0.05; done
    "$job" "$@" &
}

## Waits for every background job started by poolRun.
poolWait() {
    wait
}

## Prints a pass line and counts it.
okLine() {
    echo "ok   [$1]"
    countAdd pass
}

## Prints a fail line with the first lines of its output and counts it.
## Usage: failLine label message output
failLine() {
    echo "FAIL [$1] $2"
    [ -n "${3:-}" ] && head -8 <<<"$3"
    countAdd fail
}

## Prints a skip line and counts it.
skipLine() {
    echo "skip [$1] $2"
    countAdd skip
}

## True when `name` matches the XLANG_E2E_FILTER regular expression (unset or empty matches everything).
wantName() {
    [ -z "${XLANG_E2E_FILTER:-}" ] && return 0
    # shellcheck disable=SC2076 # a deliberate regular expression, not a literal
    [[ $1 =~ ${XLANG_E2E_FILTER} ]]
}
