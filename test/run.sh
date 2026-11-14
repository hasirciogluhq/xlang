#!/usr/bin/env bash
# End-to-end tests of the `xl` binary: every command against a real compiler and stdlib, in a temporary directory, without the
# network. Packages come from local git repositories (`source = "<dir>"`); `xl lc`, `xl std` and `xl self-update` read releases from a
# directory through XLANG_MIRROR (src/core/release.xl), laid out and checksummed like the GitHub releases.
#
# Usage: test/run.sh <xl> <xlc> <stdlib import root>
#   xl          the xl binary under test
#   xlc         a compiler binary; it is also packed as the mirror's compiler release
#   stdlib      the stdlib import root: the directory holding index.xl (`xl-std/src` of a checkout, or an installed release);
#               it is also packed, as it is, as the mirror's stdlib release
#
# `xl self-update` is also tested with a release built from this checkout's src/ as version 99.0.0.
#
# Runs on Linux, macOS and Windows (Git Bash: paths handed to native programs go through `cygpath -m`, archives through 7-Zip).
set -euo pipefail

[ "$#" -eq 3 ] || { echo "usage: $0 <xl> <xlc> <stdlib dir>" >&2; exit 2; }

case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*) windows=1; EXE=.exe; ARCHIVE=zip; os=windows ;;
  Darwin) windows=0; EXE=; ARCHIVE=tar.gz; os=darwin ;;
  *) windows=0; EXE=; ARCHIVE=tar.gz; os=linux ;;
esac
case "$(uname -m)" in
  x86_64 | amd64) arch=amd64 ;;
  arm64 | aarch64) arch=arm64 ;;
  *) echo "unsupported CPU $(uname -m)" >&2; exit 2 ;;
esac

# A path as a native program wants it: `C:/x/y` on Windows, unchanged elsewhere.
native() { if [ "$windows" = 1 ]; then cygpath -m "$1"; else printf '%s' "$1"; fi; }
absolute() { (cd "$(dirname "$1")" && printf '%s/%s' "$(pwd)" "$(basename "$1")"); }
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{ print $1 }'; else shasum -a 256 "$1" | awk '{ print $1 }'; fi; }

xl=$(absolute "$1")
xlc=$(absolute "$2")
# Git Bash runs `xlc` for `xlc.exe`, but native programs (xl checks that $XLC exists) need the real name.
if [ "$windows" = 1 ]; then
  [[ "$xl" == *.exe ]] || xl="$xl.exe"
  [[ "$xlc" == *.exe ]] || xlc="$xlc.exe"
fi
# The stdlib import root: the directory the compiler gets as `-M std=`, which holds index.xl.
std=$(cd "$3" && pwd)
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The tests own the environment: no installed toolchain, no overrides from the caller.
unset XLC XLANG_STD XLANG_MIRROR XL_VERBOSE
export XLANG_HOME
XLANG_HOME=$(native "$work/home")
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid

passed=0
failed=0
out=""
code=0

# run <command...>: runs it, keeping its combined output in $out and its exit status in $code.
run() {
  set +e
  out=$("$@" 2>&1)
  code=$?
  set -e
}

# in_app <xl arguments...>: `run` of xl inside the package test project.
in_app() {
  set +e
  out=$(cd "$app" && "$xl" "$@" 2>&1)
  code=$?
  set -e
}

# inside <dir> <command...>: `run` inside <dir>.
inside() {
  local dir=$1
  shift
  set +e
  out=$(cd "$dir" && "$@" 2>&1)
  code=$?
  set -e
}

# expect <name> <status> <text>: the last `run` exited with <status> and printed <text> (empty: anything).
expect() {
  if [ "$code" = "$2" ] && { [ -z "$3" ] || [[ "$out" == *"$3"* ]]; }; then
    passed=$((passed + 1))
    echo "ok   $1"
  else
    failed=$((failed + 1))
    echo "FAIL $1: exit $code (want $2), want output containing '$3'; got:"
    printf '%s\n' "$out" | head -n 20 | awk '{ print "     " $0 }'
  fi
}

# commit <dir> <tag>: commits everything in the repository <dir> and tags it.
commit() {
  git -C "$1" add -A
  git -C "$1" commit -q -m "$2"
  git -C "$1" tag "$2"
}

# --- the CLI itself -----------------------------------------------------------------------------------------------------------------
run "$xl" version
expect "version" 0 ""
version=$out
run "$xl" help
expect "help" 0 "usage: xl <command>"
run "$xl" nonsense
expect "unknown command" 2 "unknown command"
run "$xl" run --help
expect "command help needs no toolchain" 0 "usage: xl run"

mkdir -p "$work/hello"
cat >"$work/hello/main.xl" <<'EOF'
from "std" import println
from "std/os/args" import fromArgv

@entry
fn main(argc: i32, argv: **u8) -> i32 {
    local args = fromArgv(argc, argv)
    for a, i in args { if i > 0 { println("arg {} = {}", i, a) } }
    return (args.length() - 1) as i32
}
EOF
hello=$(native "$work/hello/main.xl")

run "$xl" check "$hello"
expect "no toolchain is E0790" 1 "E0790"

# --- check, build, run, env with XLC and XLANG_STD ------------------------------------------------------------------------------------
export XLC XLANG_STD
XLC=$(native "$xlc")
XLANG_STD=$(native "$std")

run "$xl" check "$hello"
expect "check" 0 ""
run "$xl" build "$hello" -o "$(native "$work/hello/hello$EXE")"
expect "build" 0 ""
run "$work/hello/hello$EXE" one two
expect "built program" 2 "arg 2 = two"
run "$xl" run "$hello" first "second arg" --flag
expect "run passes arguments and status" 3 "arg 3 = --flag"
inside "$work/hello" "$xl" run -- -x
expect "run main.xl of the current directory" 1 "arg 1 = -x"
run "$xl" env --json "$hello"
expect "env --json" 0 "\"std\":\"$XLANG_STD\""
run "$xl" env --bogus
expect "env rejects unknown flags" 1 "E0740"

# A prebuilt object (built without the stdlib, so it carries no second copy of it) is linked after the program root.
mkdir -p "$work/linked"
printf 'export(c) fn prebuiltAnswer() -> i32 { return 42 }\n' >"$work/linked/lib.xl"
printf 'from "std" import println\ndeclare fn prebuiltAnswer() -> i32\n@entry fn main() -> i32 {\n    println("prebuilt {}", prebuiltAnswer())\n    return 0\n}\n' >"$work/linked/main.xl"
run "$xlc" "$(native "$work/linked/lib.xl")" --emit=obj -o "$(native "$work/linked/lib.o")"
expect "object without the stdlib" 0 ""
run "$xl" build "$(native "$work/linked/main.xl")" "$(native "$work/linked/lib.o")" -o "$(native "$work/linked/app$EXE")"
expect "build links a prebuilt object" 0 ""
run "$work/linked/app$EXE"
expect "the linked program" 0 "prebuilt 42"

# --- xl test ------------------------------------------------------------------------------------------------------------------------
# A passing suite at the top, a failing suite, a file that does not build, and copies in directories the search skips.
suite="$work/suite"
mkdir -p "$suite/net/http" "$suite/build" "$suite/.cache"
cat >"$suite/a.test.xl" <<'EOF'
from "std/testing" import Suite, T, suiteInit, test, suiteRun, expectEq

fn adds(t: *T) { expectEq(t, 1 + 2, 3) }

@entry
fn main() -> i32 {
    local s: Suite
    suiteInit(&s)
    test(&s, "adds", adds)
    return suiteRun(&s)
}
EOF
sed 's/1 + 2, 3/1 + 2, 4/' "$suite/a.test.xl" >"$suite/net/fail.test.xl"
cp "$suite/a.test.xl" "$suite/net/http/ok.test.xl"
cp "$suite/a.test.xl" "$suite/build/skipped.test.xl"
cp "$suite/a.test.xl" "$suite/.cache/skipped.test.xl"
printf '@entry fn main() -> i32 { return missingName }\n' >"$suite/net/broken.test.xl"
printf 'from "std" import println\nfrom "std/os/args" import fromArgv\n@entry fn main(argc: i32, argv: **u8) -> i32 {\n    println("args {}", fromArgv(argc, argv).length() - 1)\n    return 0\n}\n' >"$suite/args.test.xl"

inside "$suite" "$xl" test --list
expect "test --list skips build/ and dot directories" 0 "net/http/ok.test.xl"
[[ "$out" != *skipped* ]] || code=9
expect "test --list leaves out skipped directories" 0 ""
inside "$suite" "$xl" test --list 'net/**' -x broken
expect "test glob and exclude" 0 "net/fail.test.xl"
[[ "$out" != *a.test.xl* && "$out" != *broken* ]] || code=9
expect "test glob and exclude select only those" 0 ""
inside "$suite" "$xl" test --list 'ok*'
expect "test glob without / matches at any depth" 0 "net/http/ok.test.xl"
inside "$suite" "$xl" test nothing-matches
expect "test pattern without files is E0711" 1 "E0711"
inside "$suite" "$xl" test -j 0
expect "test rejects -j 0" 1 "E0740"
inside "$suite" "$xl" test -o out
expect "test rejects -o" 1 "E0740"
inside "$suite" "$xl" test --no-color -q -j 2 a.test.xl net/http
expect "test passing files" 0 "2 passed"
inside "$suite" "$xl" test --no-color -j 3
expect "test reports a failed run" 1 "FAIL  net/fail.test.xl (exit 1)"
expect "test reports a failed build" 1 "FAIL  net/broken.test.xl (build failed)"
expect "test counts" 1 "2 failed, 3 passed"
inside "$suite" "$xl" test --no-color --fail-fast -j 1 -q
expect "test --fail-fast skips the rest" 1 "1 failed, 2 passed, 2 skipped"
inside "$suite" "$xl" test --no-color args -- one two
expect "test passes arguments after --" 0 "args 2"

# --- packages -----------------------------------------------------------------------------------------------------------------------
# a: v1.0.0 answers 1, v1.1.0 answers 11. b v0.1.0 needs a v1.0.0 and doubles its answer.
repos="$work/repos"
mkdir -p "$repos/a" "$repos/b"
git -C "$repos/a" init -q
printf 'export fn answer() -> i32 { return 1 }\n' >"$repos/a/index.xl"
commit "$repos/a" v1.0.0
printf 'export fn answer() -> i32 { return 11 }\n' >"$repos/a/index.xl"
commit "$repos/a" v1.1.0
git -C "$repos/b" init -q
printf 'from "example.com/t/a" import answer\nexport fn twice() -> i32 { return answer() * 2 }\n' >"$repos/b/index.xl"
# CRLF line ends, as Git for Windows checks files out with core.autocrlf: TOML accepts both.
printf '[dependencies]\r\n"example.com/t/a" = { version = "v1.0.0", source = "%s" }\r\n' "$(native "$repos/a")" >"$repos/b/xlang.toml"
commit "$repos/b" v0.1.0

app="$work/app"
mkdir -p "$app"
printf '[dependencies]\n"example.com/t/b" = { version = "v0.1.0", source = "%s" }\n' "$(native "$repos/b")" >"$app/xlang.toml"
cat >"$app/main.xl" <<'EOF'
from "std" import println
from "example.com/t/b" import twice
from "example.com/t/a" import answer

@entry
fn main() -> i32 {
    println("twice={} answer={}", twice(), answer())
    return 0
}
EOF

in_app run
expect "run before install is E0741" 1 "E0741"
in_app pkg install
expect "pkg install" 0 ""
[ -f "$app/xlang.lock" ] && grep -q 'name = "example.com/t/a"' "$app/xlang.lock" && out="lock lists a" || out="lock lacks a"
code=0; expect "lock records the indirect package" 0 "lock lists a"
in_app run
expect "run with packages" 0 "twice=2 answer=1"
in_app pkg list
expect "pkg list" 0 "example.com/t/a"
in_app env
expect "env maps packages" 0 "example.com/t/b"

# the project's own requirement of a v1.1.0 wins over b's v1.0.0: the project chooses.
printf '"example.com/t/a" = { version = "v1.1.0", source = "%s" }\n' "$(native "$repos/a")" >>"$app/xlang.toml"
in_app pkg install
expect "pkg install after a raise" 0 ""
in_app run
expect "the project's requirement wins" 0 "twice=22 answer=11"

# xlang.local.toml points a at a checkout and adds a flag.
mkdir -p "$work/a-local"
printf 'export fn answer() -> i32 { return 5 }\n' >"$work/a-local/index.xl"
printf '[modules]\n"example.com/t/a" = "../a-local"\n\n[build]\nflags = ["-DlocalFlag=true"]\n' >"$app/xlang.local.toml"
in_app run
expect "xlang.local.toml replaces a module" 0 "twice=10 answer=5"
printf '[toolchain]\ncompiler = "x"\n' >"$app/xlang.local.toml"
in_app run
expect "a bad xlang.local.toml line is E0740" 1 "E0740"
rm "$app/xlang.local.toml"

in_app pkg add example.com/t/c@v2.0.0
expect "pkg add" 0 ""
grep -q '"example.com/t/c" = "v2.0.0"' "$app/xlang.toml" && out="added" || out="not added"
code=0; expect "pkg add writes xlang.toml" 0 "added"
in_app pkg remove example.com/t/c
expect "pkg remove" 0 ""
in_app pkg add 'not a repo'
expect "pkg add rejects a bad name" 1 "E07"
in_app pkg clean
expect "pkg clean" 0 ""
in_app pkg list
expect "pkg list after clean reports the missing cache" 1 ""

# d keeps its source under src/ (`[package] root`); it is used at a tag, a commit and a branch.
mkdir -p "$repos/d/src"
git -C "$repos/d" init -q
printf '[package]\nroot = "src/"\n' >"$repos/d/xlang.toml"
printf 'export fn dee() -> i32 { return 3 }\n' >"$repos/d/src/index.xl"
commit "$repos/d" nightly
first=$(git -C "$repos/d" rev-parse HEAD)
git -C "$repos/d" checkout -q -b dev
printf 'export fn dee() -> i32 { return 4 }\n' >"$repos/d/src/index.xl"
git -C "$repos/d" commit -q -am four
app="$work/app-d"
mkdir -p "$app"
printf 'from "std" import println\nfrom "example.com/t/d" import dee\n@entry\nfn main() -> i32 {\n    println("dee={}", dee())\n    return 0\n}\n' >"$app/main.xl"
dep() { printf '[dependencies]\n"example.com/t/d" = { %s, source = "%s" }\n' "$1" "$(native "$repos/d")" >"$app/xlang.toml"; }
dep 'tag = "nightly"'
in_app pkg install
in_app run
expect "a tag dependency resolves under [package] root" 0 "dee=3"
dep "rev = \"$first\""
in_app pkg install
in_app run
expect "a rev dependency" 0 "dee=3"
dep 'branch = "dev"'
in_app pkg install
grep -q 'branch = "dev"' "$app/xlang.lock" && in_app run
expect "a branch dependency is locked" 0 "dee=4"
printf 'export fn dee() -> i32 { return 5 }\n' >"$repos/d/src/index.xl"
git -C "$repos/d" commit -q -am five
in_app pkg install
in_app run
expect "install keeps the locked branch commit" 0 "dee=4"
in_app pkg update
in_app run
expect "update moves a branch to its head" 0 "dee=5"
dep 'tag = "nightly", branch = "dev"'
in_app pkg install
expect "two references are E0740" 1 "E0740"
dep 'rev = "abc123"'
in_app pkg install
expect "a short rev is E0742" 1 "E0742"
app="$work/app"

# --- xl lc and xl std: the compiler and the stdlib from a mirror ------------------------------------------------------------------
mirror="$work/mirror"
tag=v0.0.1
xlcDir="$mirror/hasirciogluhq/xlc/$tag"
stdDir="$mirror/hasirciogluhq/xl-std/$tag"
mkdir -p "$xlcDir" "$stdDir" "$work/pack/xlc" "$work/pack/xl-std_0.0.1"
cp "$xlc" "$work/pack/xlc/xlc$EXE"
xlcAsset="xlc_0.0.1_${os}_${arch}.$ARCHIVE"
if [ "$ARCHIVE" = zip ]; then
  (cd "$work/pack/xlc" && 7z a -tzip -bd -bso0 "$xlcDir/$xlcAsset" "xlc$EXE")
else
  tar -czf "$xlcDir/$xlcAsset" -C "$work/pack/xlc" xlc
fi
# The stdlib release holds index.xl and the area directories at its root (xl-std/.github/workflows/release.yml), which is what
# the import root of a checkout is too.
cp -R "$std/." "$work/pack/xl-std_0.0.1/"
tar -czf "$stdDir/xl-std_0.0.1.tar.gz" -C "$work/pack" xl-std_0.0.1
printf '%s  %s\n' "$(sha "$xlcDir/$xlcAsset")" "$xlcAsset" >"$xlcDir/checksums.txt"
printf '%s  %s\n' "$(sha "$stdDir/xl-std_0.0.1.tar.gz")" "xl-std_0.0.1.tar.gz" >"$stdDir/checksums.txt"

unset XLC XLANG_STD
export XLANG_MIRROR
XLANG_MIRROR=$(native "$mirror")
run "$xl" lc install
expect "lc install" 0 "active: xlc $tag"
run "$xl" lc list
expect "lc list" 0 "* $tag"
run "$xl" std install
expect "std install" 0 "active: std $tag"
run "$xl" std list
expect "std list" 0 "* $tag"
run "$xl" run "$hello" installed
expect "run with the installed toolchain" 1 "arg 1 = installed"
run "$xl" env --json "$hello"
expect "env names the installed tags" 0 "\"xlcTag\":\"$tag\""
run "$xl" lc install v9
expect "lc install of a missing tag is E0744" 1 "E0744"
run "$xl" std install v9
expect "std install of a missing tag is E0744" 1 "E0744"
run "$xl" lc use v7.0.0
expect "lc use of a tag not installed is E0790" 1 "E0790"
run "$xl" std use v7.0.0
expect "std use of a tag not installed is E0790" 1 "E0790"
run "$xl" lc update
expect "lc update" 0 "already up to date"
run "$xl" std update
expect "std update" 0 "already up to date"

# A damaged asset is refused before anything is unpacked.
printf '%s  %s\n' "0000000000000000000000000000000000000000000000000000000000000000" "xl-std_0.0.1.tar.gz" >"$stdDir/checksums.txt"
run env XLANG_HOME="$(native "$work/home2")" "$xl" std install
expect "a checksum mismatch is E0792" 1 "E0792"

# --- xl self-update -----------------------------------------------------------------------------------------------------------------
xlDir="$mirror/hasirciogluhq/xlang/v99.0.0"
mkdir -p "$work/pack/xl" "$work/bin"
cp "$xl" "$work/bin/xl$EXE"
run "$work/bin/xl$EXE" self-update --check
expect "self-update without a release is E0791" 1 "E0791"
mkdir -p "$xlDir"
# The new release reports 99.0.0 only if it is that version: a copy of xl that says otherwise is refused.
cp "$xl" "$work/pack/xl/xl$EXE"
xlAsset="xl_99.0.0_${os}_${arch}.$ARCHIVE"
if [ "$ARCHIVE" = zip ]; then
  (cd "$work/pack/xl" && 7z a -tzip -bd -bso0 "$xlDir/$xlAsset" "xl$EXE")
else
  tar -czf "$xlDir/$xlAsset" -C "$work/pack/xl" xl
fi
printf '%s  %s\n' "$(sha "$xlDir/$xlAsset")" "$xlAsset" >"$xlDir/checksums.txt"
run "$work/bin/xl$EXE" self-update --check
expect "self-update --check" 0 "v99.0.0 is available"
run "$work/bin/xl$EXE" self-update
expect "self-update refuses a binary of another version" 1 "E0793"
run "$work/bin/xl$EXE" version
expect "the refused update leaves xl in place" 0 "$version"

# The real 99.0.0: xl built from this checkout with that version.
inside "$root/src" "$xlc" main.xl -M "std=$(native "$std")" --import std -DfeatureDefaultAllocator=true '-DxlVersion="99.0.0"' -o "$(native "$work/pack/xl/xl$EXE")"
expect "build xl 99.0.0" 0 ""
rm -f "$xlDir/$xlAsset"
if [ "$ARCHIVE" = zip ]; then
  (cd "$work/pack/xl" && 7z a -tzip -bd -bso0 "$xlDir/$xlAsset" "xl$EXE")
else
  tar -czf "$xlDir/$xlAsset" -C "$work/pack/xl" xl
fi
printf '%s  %s\n' "$(sha "$xlDir/$xlAsset")" "$xlAsset" >"$xlDir/checksums.txt"
run "$work/bin/xl$EXE" self-update
expect "self-update" 0 "-> v99.0.0"
run "$work/bin/xl$EXE" version
expect "the updated xl runs" 0 "99.0.0"
run "$work/bin/xl$EXE" self-update
expect "self-update when up to date" 0 "up to date"

echo
echo "$passed passed, $failed failed"
[ "$failed" = 0 ]
