#!/bin/bash
# Package system end to end (sourced by run.sh; uses $xlang, $work, pass, fail). Needs `git`.
# Every repository is a local git repository: no network. A dependency's `source` points at it.
if ! command -v git >/dev/null 2>&1; then
  echo "skip [packages] git not found"; skip=$((skip+1))
else
pk="$work/pk"
rm -rf "$pk"; mkdir -p "$pk"
g() { git -c user.email=t@t -c user.name=t -c commit.gpgsign=false -c tag.gpgsign=false "$@"; }
pkg_ok()   { echo "ok   [packages/$1]"; pass=$((pass+1)); }
pkg_fail() { echo "FAIL [packages/$1] $2"; [ -n "${3:-}" ] && echo "$3" | head -8; fail=$((fail+1)); }

# --- a pure-xl library with release tags
lib="$pk/lib"; mkdir -p "$lib/sub"; (cd "$lib" && g init -q -b main)
echo 'export fn answer() -> i32 { return 41 }' >"$lib/index.xl"
echo 'export fn deep() -> i32 { return 5 }' >"$lib/sub/index.xl"
(cd "$lib" && g add . && g commit -q -m one && g tag v1.0.0)
echo 'export fn answer() -> i32 { return 42 }' >"$lib/index.xl"
(cd "$lib" && g add . && g commit -q -m two && g tag 1.1.0)
echo 'export fn answer() -> i32 { return 0 }' >"$lib/index.xl"
(cd "$lib" && g add . && g commit -q -m test && g tag v1.2.0-test && g tag nightly)

proj="$pk/proj"; mkdir -p "$proj"
cat >"$proj/main.xl" <<'XL'
from "example.org/u/lib" import answer
from "example.org/u/lib/sub" import deep
fn main() -> i32 { return answer() + deep() }
XL
cat >"$proj/xlang.toml" <<TOML
[dependencies]
"example.org/u/lib" = { version = "latest", source = "$lib" }
TOML

# build before install: refused, with the fix
out=$("$xlang" build "$proj/main.xl" -o "$proj/app" 2>&1)
if [ $? -ne 0 ] && echo "$out" | grep -q "E0741"; then pkg_ok "build-needs-install"; else pkg_fail "build-needs-install" "expected E0741" "$out"; fi

# install picks the newest stable release (1.1.0): -test, pre-release and non-version tags are ignored
out=$(cd "$proj" && "$xlang" package install 2>&1)
if [ $? -eq 0 ] && [ -f "$proj/xlang.lock" ] && grep -q 'version = "1.1.0"' "$proj/xlang.lock"; then
  pkg_ok "install-latest"; else pkg_fail "install-latest" "lock lacks 1.1.0" "$out"; fi
"$xlang" run "$proj/main.xl" >/dev/null 2>&1; code=$?
if [ "$code" = 47 ]; then pkg_ok "run-installed"; else pkg_fail "run-installed" "exit $code (want 47)"; fi

# a new release is not picked up until `update`; update rewrites the manifest and keeps `source`
echo 'export fn answer() -> i32 { return 43 }' >"$lib/index.xl"
(cd "$lib" && g add . && g commit -q -m three && g tag v1.3.0)
"$xlang" run "$proj/main.xl" >/dev/null 2>&1; code=$?
if [ "$code" = 47 ]; then pkg_ok "lock-is-stable"; else pkg_fail "lock-is-stable" "exit $code (want 47)"; fi
out=$(cd "$proj" && "$xlang" package update 2>&1)
"$xlang" run "$proj/main.xl" >/dev/null 2>&1; code=$?
if [ "$code" = 48 ] && grep -q 'v1.3.0' "$proj/xlang.toml" && grep -q "source" "$proj/xlang.toml"; then
  pkg_ok "update"; else pkg_fail "update" "exit $code (want 48)" "$out"; fi

# list, cache and clean
out=$(cd "$proj" && "$xlang" package list 2>&1)
if echo "$out" | grep -q "example.org/u/lib  v1.3.0"; then pkg_ok "list"; else pkg_fail "list" "no entry" "$out"; fi
(cd "$proj" && "$xlang" package clean >/dev/null 2>&1)
"$xlang" build "$proj/main.xl" -o "$proj/app" >/dev/null 2>&1
if [ $? -ne 0 ] && [ ! -d "$proj/.xlang/cache" ]; then pkg_ok "clean"; else pkg_fail "clean" "cache survived or build passed"; fi
(cd "$proj" && "$xlang" package install >/dev/null 2>&1)
"$xlang" run "$proj/main.xl" >/dev/null 2>&1; code=$?
if [ "$code" = 48 ]; then pkg_ok "reinstall-from-lock"; else pkg_fail "reinstall-from-lock" "exit $code (want 48)"; fi

# the version rule: a branch name or a -test tag is refused before anything is downloaded
cp "$proj/xlang.toml" "$proj/xlang.toml.keep"
sed -i.bak 's/v1.3.0/main/' "$proj/xlang.toml"
out=$("$xlang" build "$proj/main.xl" -o "$proj/app" 2>&1)
if echo "$out" | grep -q "E0742"; then pkg_ok "bad-tag"; else pkg_fail "bad-tag" "expected E0742" "$out"; fi
cp "$proj/xlang.toml.keep" "$proj/xlang.toml"; rm -f "$proj/xlang.toml.bak"

# `repo@version` installs a -test tag when it is named; the source of the entry is kept
out=$(cd "$proj" && "$xlang" package install example.org/u/lib@v1.2.0-test 2>&1)
"$xlang" run "$proj/main.xl" >/dev/null 2>&1; code=$?
if [ "$code" = 5 ] && grep -q 'v1.2.0-test' "$proj/xlang.toml" && grep -q "source" "$proj/xlang.toml"; then
  pkg_ok "explicit-test-tag"; else pkg_fail "explicit-test-tag" "exit $code (want 5)" "$out"; fi
cp "$proj/xlang.toml.keep" "$proj/xlang.toml"

# a repository with only -test / pre-release tags has no latest: install fails, nothing is written
only="$pk/only"; mkdir -p "$only"; (cd "$only" && g init -q -b main)
echo 'export fn o() -> i32 { return 1 }' >"$only/index.xl"
(cd "$only" && g add . && g commit -q -m o && g tag v1.0.0-test && g tag v1.0.0-rc.1)
mkdir -p "$pk/onlyproj"
echo "[dependencies]
\"example.org/u/only\" = { version = \"latest\", source = \"$only\" }" >"$pk/onlyproj/xlang.toml"
out=$(cd "$pk/onlyproj" && "$xlang" package install 2>&1)
if [ $? -ne 0 ] && echo "$out" | grep -q "no stable release tag" && [ ! -f "$pk/onlyproj/xlang.lock" ]; then
  pkg_ok "only-test-tags"; else pkg_fail "only-test-tags" "expected a failure" "$out"; fi

# an import of a repository that xlang.toml does not list
mkdir -p "$pk/undeclared"
echo 'from "example.org/u/other" import x
fn main() -> i32 { return 0 }' >"$pk/undeclared/main.xl"
echo '[package]
name = "undeclared"' >"$pk/undeclared/xlang.toml"
out=$("$xlang" check "$pk/undeclared/main.xl" 2>&1)
if echo "$out" | grep -q "E0745"; then pkg_ok "undeclared-import"; else pkg_fail "undeclared-import" "expected E0745" "$out"; fi

# --- a package with a prebuilt native library for this platform
os=$(uname -s | tr 'A-Z' 'a-z'); [ "$os" = darwin ] && os=macos
arch=$(uname -m); [ "$arch" = arm64 ] && arch=aarch64
nat="$pk/nat"; mkdir -p "$nat/lib"; (cd "$nat" && g init -q -b main)
echo 'export(c) fn natAdd(a: i32, b: i32) -> i32 { return a + b + 100 }' >"$pk/natimpl.xl"
# (a prebuilt xlang library must be built with --no-std, or it carries its own copy of the stdlib)
if "$xlang" build "$pk/natimpl.xl" --no-std --emit=static -o "$nat/lib/libnat.a" >/dev/null 2>&1; then
  cat >"$nat/index.xl" <<'XL'
declare fn natAdd(a: i32, b: i32) -> i32
export fn add(a: i32, b: i32) -> i32 { return natAdd(a, b) }
XL
  cat >"$nat/xlang.toml" <<TOML
[[native]]
os = "$os"
arch = "$arch"
link = ["lib/libnat.a"]
TOML
  (cd "$nat" && g add . && g commit -q -m nat && g tag v0.1.0)
  mkdir -p "$pk/natproj"
  echo 'from "example.org/u/nat" import add
fn main() -> i32 { return add(1, 2) }' >"$pk/natproj/main.xl"
  cat >"$pk/natproj/xlang.toml" <<TOML
[dependencies]
"example.org/u/nat" = { version = "v0.1.0", source = "$nat" }
TOML
  (cd "$pk/natproj" && "$xlang" package install >/dev/null 2>&1)
  "$xlang" run "$pk/natproj/main.xl" >/dev/null 2>&1; code=$?
  if [ "$code" = 103 ]; then pkg_ok "native-binary"; else pkg_fail "native-binary" "exit $code (want 103)"; fi
else
  pkg_fail "native-binary" "could not build the static library"
fi

# --- a standard library from a repository, and a local one
std="$pk/mystd"; mkdir -p "$std"; (cd "$std" && g init -q -b main)
cp -R "$root/std/." "$std/"
(cd "$std" && g add . && g commit -q -m std && g tag v0.0.1)
mkdir -p "$pk/stdproj"
echo 'fn main() -> i32 { return 7 }' >"$pk/stdproj/main.xl"
cat >"$pk/stdproj/xlang.toml" <<TOML
[std]
package = "example.org/u/mystd"
version = "v0.0.1"
source = "$std"
TOML
out=$("$xlang" check "$pk/stdproj/main.xl" 2>&1)
if echo "$out" | grep -q "E0741"; then pkg_ok "std-needs-install"; else pkg_fail "std-needs-install" "expected E0741" "$out"; fi
(cd "$pk/stdproj" && "$xlang" package install >/dev/null 2>&1)
"$xlang" run "$pk/stdproj/main.xl" >/dev/null 2>&1; code=$?
if [ "$code" = 7 ] && grep -q "std = true" "$pk/stdproj/xlang.lock"; then pkg_ok "std-from-repository"; else pkg_fail "std-from-repository" "exit $code (want 7)"; fi
"$xlang" run "$pk/stdproj/main.xl" --std="$std" >/dev/null 2>&1; code=$?
if [ "$code" = 7 ]; then pkg_ok "std-flag-directory"; else pkg_fail "std-flag-directory" "exit $code (want 7)"; fi
fi
