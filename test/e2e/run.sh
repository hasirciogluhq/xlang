#!/bin/bash
# End-to-end tests: build each program with the compiler and run it.
#   test/e2e/run.sh [path/to/xlang]
# Runs natively; when Docker is available, also on Linux aarch64 and x86_64 (cross-built); when
# `cxrun` (CrossOver/Wine: `cxrun <exe>` runs a Windows executable and returns its exit status) is on
# PATH, also as Windows x86_64 executables. XLANG_E2E_NET=1 adds the tests that need the Internet.
# XLANG_E2E_S3=1 adds s3.xl, which needs an S3-compatible server on 127.0.0.1:18333 with the access key
# testkey / testsecret, for example SeaweedFS:
#   docker run -d -p 18333:8333 -v "$PWD/test/e2e/seaweedfs.json:/etc/seaweedfs/s3.json" chrislusf/seaweedfs server -s3 -s3.config=/etc/seaweedfs/s3.json
# A Docker registry failure is a SKIP, not a FAIL: alpine:3 is pulled once up front (3 attempts), and a
# container that never starts (exit 125) is retried once. Only a wrong result or a build error fails.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
xlang=${1:-$root/outputs/xlang}
work=$(mktemp -d)
trap 'rm -rf "$work"; [ -n "${spid:-}" ] && kill "$spid" 2>/dev/null' EXIT
pass=0; fail=0; skip=0

# name | expected exit code | expected stdout (a `grep -F` substring) | uses OS threads | runs on Windows
tests=(
  "language|0||0|1"
  "controlflow|0|controlflow ok|0|1"
  "enums|0|enums ok|0|1"
  "router|0|router ok|0|1"
  "stdlib|0|499500|0|1"
  "fsjson|0||0|1"
  "argv|0|argv ok|0|0"
  "panics|134|before|0|0"
  "vecpanic|134|before|0|0"
  "threads|0|40000|1|0"
  "try|0|try ok|0|0"
  "sched|0|20100|0|0"
  "sched_io|0|5|0|0"
  "bitops|0|bitops ok|0|1"
  "sha256|0|sha256 ok|0|1"
  "sha512|0|sha512 ok|0|1"
  "hmac|0|hmac ok|0|1"
  "aesgcm|0|aesgcm ok|0|1"
  "chacha20poly1305|0|chacha20poly1305 ok|0|1"
  "x25519|0|x25519 ok|0|1"
  "ecdsa|0|ecdsa ok|0|1"
  "strings|0|strings ok|0|1"
  "strtools|0|strtools ok|0|1"
  "overload|0|overload ok|0|1"
  "raii|0|raii ok|0|1"
  "generics|0|generics ok|0|1"
  "collections|0|collections ok|0|1"
  "math_vectors|0|math_vectors ok|0|1"
  "math_matrix2|0|math_matrix2 ok|0|1"
  "math_matrix3|0|math_matrix3 ok|0|1"
  "math_matrix4|0|math_matrix4 ok|0|1"
  "math_quat|0|math_quat ok|0|1"
  "math_transform|0|math_transform ok|0|1"
  "big|0|big ok|0|1"
  "print|0|print ok|0|1"
  "printalloc|0|printalloc ok|0|1"
  "variadic|0|variadic ok|0|1"
  "printpanic|134|format: 2 placeholders, got 1 arguments|0|0"
  "init|0|init ok|0|1"
  "initset|0|initset ok|0|1"
  "cyclic|0|cyclic ok|0|1"
  "entry_order|3|initA initB entry finiB finiA|0|0"
)
[ "${XLANG_E2E_NET:-}" = 1 ] && tests+=("https|0|https ok|0|1")

# server program | client program | text the client prints on success. The server runs in the background
# (it serves until the client asks it to quit); the client's exit status and output are the result.
# Each pair runs with an RSA and an ECDSA certificate (`-D tlsEc=1`) on its own port.
pairs=(
  "tlsserver|tlsclient|tlsclient ok"
  "httpsserver|httpsclient|httpsclient ok"
)
port=15000
win_triple=x86_64-pc-windows-msvc

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

# Pulls alpine:3 for both platforms before any test: up to 3 attempts each, waiting 5 and then 15
# seconds between attempts. Returns 1 when a pull still fails.
pull_images() {
  local plat try delay=(5 15 30)
  for plat in arm64 amd64; do
    for try in 1 2 3; do
      docker pull -q --platform "linux/$plat" alpine:3 >/dev/null 2>&1 && break
      [ "$try" = 3 ] && return 1
      sleep "${delay[$((try - 1))]}"
    done
  done
}

# Docker tests per run: two Linux legs per test, and four legs per server/client pair (2 certificates
# x 2 architectures). They are counted as skipped when the registry is unavailable.
docker_legs=$(( 2 * ${#tests[@]} + 4 * ${#pairs[@]} ))
docker_ok=0
if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  if pull_images; then
    docker_ok=1
  else
    echo "docker: alpine:3 unavailable, $docker_legs tests skipped"
    skip=$((skip + docker_legs))
  fi
fi

docker_run() {  # platform binary
  local plat=$1 bin=$2 cid
  cid=$(docker run -d --platform "linux/$plat" -v "$(dirname "$bin")":/w alpine:3 "/w/$(basename "$bin")" 2>"$work/docker.err") || return 125
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

# S3 runs natively only: the server listens on the host's loopback, which a Docker container does not reach.
if [ "${XLANG_E2E_S3:-}" = 1 ]; then
  if "$xlang" build "$here/s3.xl" -o "$work/s3" 2>"$work/s3.err"; then
    run_one "$host_os/s3" "$work/s3" 0 "s3 ok" env
  else
    echo "FAIL [$host_os/s3] build"; head -8 "$work/s3.err"; fail=$((fail+1))
  fi
fi

for t in "${tests[@]}"; do
  IFS='|' read -r name want text threads win <<<"$t"
  src=$here/$name.xl
  # native (threads come from libSystem pthreads on macOS and from clone on Linux)
  if "$xlang" build "$src" -o "$work/$name" 2>"$work/$name.err"; then
    run_one "$host_os/$name" "$work/$name" "$want" "$text" env
  else
    echo "FAIL [$host_os/$name] build"; head -8 "$work/$name.err"; fail=$((fail+1))
  fi
  # Linux through Docker
  if [ "$docker_ok" = 1 ]; then
    for spec in "arm64:aarch64-unknown-linux-gnu" "amd64:x86_64-unknown-linux-gnu"; do
      plat=${spec%%:*}; triple=${spec#*:}
      if "$xlang" build "$src" --target "$triple" -o "$work/$name.$plat" 2>"$work/$name.$plat.err"; then
        out=$(docker_run "$plat" "$work/$name.$plat"); code=$?
        # 125: the container never started (registry or daemon error); retry once, then SKIP
        [ "$code" = 125 ] && { out=$(docker_run "$plat" "$work/$name.$plat"); code=$?; }
        if [ "$code" = 125 ]; then
          echo "skip [linux-$plat/$name] docker: $(head -1 "$work/docker.err")"; skip=$((skip+1))
        elif [ "$code" != "$want" ] || { [ -n "$text" ] && ! grep -qF "$text" <<<"$out"; }; then
          echo "FAIL [linux-$plat/$name] exit $code"; echo "$out" | head -5; fail=$((fail+1))
        else
          echo "ok   [linux-$plat/$name]"; pass=$((pass+1))
        fi
      else
        echo "FAIL [linux-$plat/$name] build"; head -8 "$work/$name.$plat.err"; fail=$((fail+1))
      fi
    done
  fi
  # Windows through CrossOver/Wine
  if [ "$win" = 1 ] && command -v cxrun >/dev/null 2>&1; then
    if "$xlang" build "$src" --target "$win_triple" -o "$work/$name.exe" 2>"$work/$name.exe.err"; then
      out=$(cxrun "$work/$name.exe" 2>&1 | grep -v dosdevices; exit "${PIPESTATUS[0]}"); code=$?
      if [ "$code" != "$want" ] || { [ -n "$text" ] && ! grep -qF "$text" <<<"$out"; }; then
        echo "FAIL [windows/$name] exit $code (want $want)"; echo "$out" | head -5; fail=$((fail+1))
      else
        echo "ok   [windows/$name]"; pass=$((pass+1))
      fi
    else
      echo "FAIL [windows/$name] build"; head -8 "$work/$name.exe.err"; fail=$((fail+1))
    fi
  fi
done

# Server/client pairs (TLS 1.3 server, HTTPS server): see `pairs` above.
run_pair() {   # label server-bin client-bin text mode(native|docker:PLAT|windows) port
  local label=$1 srv=$2 cli=$3 text=$4 mode=$5 port=$6 out code
  case "$mode" in
    native)
      "$srv" >"$work/srv.log" 2>&1 & spid=$!
      # wait until the server accepts on its port (up to 10 s), instead of a fixed sleep that races a loaded machine
      for _ in $(seq 100); do (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null && break; sleep 0.1; done
      out=$("$cli" 2>&1); code=$?
      ;;
    docker:*)
      local plat=${mode#docker:} try
      for try in 1 2; do   # 125: the container never started; retried once, then SKIP
        out=$(docker run --rm --platform "linux/$plat" -v "$(dirname "$srv")":/w alpine:3 sh -c \
          "/w/$(basename "$srv") >/dev/null 2>&1 & sleep 1; /w/$(basename "$cli")" 2>&1); code=$?
        [ "$code" != 125 ] && break
      done
      if [ "$code" = 125 ]; then
        echo "skip [$label] docker: $(head -1 <<<"$out")"; skip=$((skip+1)); return
      fi
      ;;
    windows)
      cxrun "$srv" >"$work/srv.log" 2>&1 & spid=$!
      sleep 6
      out=$(cxrun "$cli" 2>&1 | grep -v dosdevices; exit "${PIPESTATUS[0]}"); code=$?
      ;;
  esac
  if [ -n "${spid:-}" ]; then
    # the server ends itself after `/quit`; give it a moment, then make sure it is gone
    for _ in 1 2 3 4 5 6; do kill -0 "$spid" 2>/dev/null || break; sleep 0.5; done
    kill "$spid" 2>/dev/null; wait "$spid" 2>/dev/null; spid=
  fi
  if [ "$code" != 0 ] || ! grep -qF "$text" <<<"$out"; then
    echo "FAIL [$label] exit $code"; echo "$out" | head -5; fail=$((fail+1))
  else
    echo "ok   [$label]"; pass=$((pass+1))
  fi
}

for p in "${pairs[@]}"; do
  IFS='|' read -r srvname cliname text <<<"$p"
  for cert in rsa ec; do
    extra=(); [ "$cert" = ec ] && extra=(-D tlsEc=1)
    port=$((port + 1))
    legs=("native:$host_os")
    [ "$docker_ok" = 1 ] && legs+=("docker:arm64" "docker:amd64")
    command -v cxrun >/dev/null 2>&1 && legs+=("windows:windows")
    for leg in "${legs[@]}"; do
      mode=${leg%%:*}; arg=${leg#*:}
      target=(); suffix=$arg; runmode=native
      case "$mode" in
        docker)
          if [ "$arg" = arm64 ]; then target=(--target aarch64-unknown-linux-gnu); else target=(--target x86_64-unknown-linux-gnu); fi
          runmode=docker:$arg ;;
        windows) target=(--target "$win_triple"); runmode=windows; suffix=windows ;;
      esac
      ext=""; [ "$mode" = windows ] && ext=.exe
      s=$work/$srvname.$cert.$suffix$ext; c=$work/$cliname.$cert.$suffix$ext
      # (`${a[@]+"${a[@]}"}`: an empty array is not an unbound variable under `set -u` in bash 3.2)
      if "$xlang" build "$here/$srvname.xl" ${target[@]+"${target[@]}"} -D "tlsPort=$port" ${extra[@]+"${extra[@]}"} -o "$s" 2>"$work/pair.err" &&
         "$xlang" build "$here/$cliname.xl" ${target[@]+"${target[@]}"} -D "tlsPort=$port" ${extra[@]+"${extra[@]}"} -o "$c" 2>>"$work/pair.err"; then
        run_pair "$suffix/$srvname+$cliname/$cert" "$s" "$c" "$text" "$runmode" "$port"
      else
        echo "FAIL [$suffix/$srvname+$cliname/$cert] build"; head -8 "$work/pair.err"; fail=$((fail+1))
      fi
    done
  done
done

# Entry-model rules, checked at compile time: name | build flags | expected (a diagnostic code that the
# build output must contain, or `ok`: the build must succeed). Native only; no run, no Docker.
diags=(
  "diag_noentry||E0739"
  "diag_twoentry||E0753"
  "diag_badsig||E0752"
  "diag_init_args||E0729"
  "diag_static_entry|--emit=static|E0754"
  "diag_shared_args|--emit=shared|E0755"
  "entry_order|--emit=ir|ok"
  "shared_hook|--emit=static|ok"
)
for d in "${diags[@]}"; do
  IFS='|' read -r name flags want <<<"$d"
  out=$("$xlang" build "$here/$name.xl" $flags -o "$work/$name.out" 2>&1); code=$?
  if { [ "$want" = ok ] && [ "$code" = 0 ]; } || { [ "$want" != ok ] && [ "$code" != 0 ] && grep -qF "$want" <<<"$out"; }; then
    echo "ok   [diag/$name]"; pass=$((pass+1))
  else
    echo "FAIL [diag/$name] want $want"; echo "$out" | head -5; fail=$((fail+1))
  fi
done

# Shared-library load hook: the @init of a library runs when it is loaded before the host's @entry.
# Exercised on macOS only (DYLD_INSERT_LIBRARIES); Linux and Windows are open in docs/known-limitations.md.
if [ "$host_os" = Darwin ]; then
  if "$xlang" build "$here/shared_hook.xl" --emit=shared -o "$work/libshared_hook.dylib" 2>"$work/hook.err" &&
     "$xlang" build "$here/shared_host.xl" -o "$work/shared_host" 2>>"$work/hook.err"; then
    out=$(DYLD_INSERT_LIBRARIES="$work/libshared_hook.dylib" "$work/shared_host" 2>&1); code=$?
    if [ "$code" = 0 ] && [ "$(head -1 <<<"$out")" = "lib up" ] && grep -qF "host" <<<"$out"; then
      echo "ok   [$host_os/shared_hook load]"; pass=$((pass+1))
    else
      echo "FAIL [$host_os/shared_hook load] exit $code"; echo "$out" | head -5; fail=$((fail+1))
    fi
  else
    echo "FAIL [$host_os/shared_hook load] build"; head -8 "$work/hook.err"; fail=$((fail+1))
  fi
fi

# Package system (local git repositories, no network): see packages.sh.
. "$here/packages.sh"

echo "e2e: $pass passed, $fail failed, $skip skipped"
[ "$fail" = 0 ]
