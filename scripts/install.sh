#!/usr/bin/env bash
# Install the xlang compiler (macOS and Linux, arm64 and amd64).
#
#   curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.sh | bash
#
# Options (or the matching environment variables):
#   --version <x.y.z>   XLANG_VERSION      release to install (default: the latest release)
#   --dir <path>        XLANG_INSTALL_DIR  where `xlang` is placed (default: $HOME/.local/bin)
#   --no-completions    XLANG_NO_COMPLETIONS=1  skip the shell tab-completion setup
#   -h, --help
#
# The script picks the archive for this machine's OS and CPU, checks it against the release's
# checksums.txt, tests the new binary and only then moves it into place.
set -euo pipefail

REPO="hasirciogluhq/xlang"
version="${XLANG_VERSION:-}"
dir="${XLANG_INSTALL_DIR:-$HOME/.local/bin}"
completions=1
[ -n "${XLANG_NO_COMPLETIONS:-}" ] && completions=0

if [ -t 2 ] && [ -z "${NO_COLOR:-}" ]; then
  bold=$'\033[1m'; red=$'\033[1;31m'; green=$'\033[1;32m'; cyan=$'\033[1;36m'; dim=$'\033[2m'; reset=$'\033[0m'
else
  bold=""; red=""; green=""; cyan=""; dim=""; reset=""
fi

step() { printf '%s==>%s %s%s%s\n' "$cyan" "$reset" "$bold" "$*" "$reset" >&2; }
note() { printf '    %s%s%s\n' "$dim" "$*" "$reset" >&2; }
fail() { printf '%serror:%s %s\n' "$red" "$reset" "$*" >&2; exit 1; }

usage() {
  cat >&2 <<'EOF'
Install the xlang compiler (macOS and Linux, arm64 and amd64).

  curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.sh | bash

Options (or the matching environment variables):
  --version <x.y.z>   XLANG_VERSION      release to install (default: the latest release)
  --dir <path>        XLANG_INSTALL_DIR  where `xlang` is placed (default: $HOME/.local/bin)
  --no-completions    XLANG_NO_COMPLETIONS=1  skip the shell tab-completion setup
  -h, --help
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --version) [ "$#" -ge 2 ] || fail "--version needs a value"; version="$2"; shift 2 ;;
    --dir) [ "$#" -ge 2 ] || fail "--dir needs a value"; dir="$2"; shift 2 ;;
    --no-completions) completions=0; shift ;;
    -h | --help) usage; exit 0 ;;
    *) fail "unknown option '$1' (try --help)" ;;
  esac
done

# --- host OS and CPU -------------------------------------------------------------------------
case "$(uname -s)" in
  Darwin) os="darwin" ;;
  Linux) os="linux" ;;
  MINGW* | MSYS* | CYGWIN*)
    fail "there is no native Windows build of the compiler yet; install inside WSL (Ubuntu, ...) and run this script there" ;;
  *) fail "unsupported operating system: $(uname -s)" ;;
esac

case "$(uname -m)" in
  arm64 | aarch64) arch="arm64" ;;
  x86_64 | amd64) arch="amd64" ;;
  *) fail "unsupported CPU: $(uname -m)" ;;
esac
# A shell started under Rosetta reports x86_64 on Apple silicon; install the native build.
if [ "$os" = "darwin" ] && [ "$arch" = "amd64" ] && [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = "1" ]; then
  arch="arm64"
fi

# --- tools -------------------------------------------------------------------------------------
command -v curl >/dev/null 2>&1 || fail "curl is required"
command -v tar >/dev/null 2>&1 || fail "tar is required"
if command -v sha256sum >/dev/null 2>&1; then
  sha256() { sha256sum "$1" | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then
  sha256() { shasum -a 256 "$1" | awk '{print $1}'; }
else
  fail "sha256sum or shasum is required to verify the download"
fi

# --- which release -----------------------------------------------------------------------------
if [ -z "$version" ]; then
  step "Looking up the latest release"
  json="$(curl -fsSL --max-time 20 -H 'Accept: application/vnd.github+json' \
    "https://api.github.com/repos/$REPO/releases/latest")" ||
    fail "could not read the latest release (none is published yet, or the network is unreachable)"
  tag="$(printf '%s' "$json" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
  [ -n "$tag" ] || fail "the release answer has no tag_name"
  version="${tag#v}"
else
  version="${version#v}"
  tag="v$version"
fi

asset="xlang_${version}_${os}_${arch}.tar.gz"
base="https://github.com/$REPO/releases/download/$tag"
step "Installing xlang $version for $os/$arch"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

step "Downloading $asset"
curl -fSL --progress-bar -o "$tmp/$asset" "$base/$asset" || fail "download failed: $base/$asset"
curl -fsSL -o "$tmp/checksums.txt" "$base/checksums.txt" || fail "download failed: $base/checksums.txt"

step "Verifying checksum"
expected="$(awk -v name="$asset" '$2 == name {print tolower($1)}' "$tmp/checksums.txt" | head -n 1)"
[ -n "$expected" ] || fail "checksums.txt has no entry for $asset; refusing to install an unverified binary"
actual="$(sha256 "$tmp/$asset")"
[ "$actual" = "$expected" ] || fail "checksum mismatch for $asset (expected $expected, got $actual)"
note "sha256 $expected"

step "Installing to $dir"
tar -xzf "$tmp/$asset" -C "$tmp" || fail "could not extract $asset"
[ -f "$tmp/xl" ] || fail "the archive does not contain an 'xl' binary"
chmod 0755 "$tmp/xl"
"$tmp/xl" --version >/dev/null 2>&1 || fail "the downloaded binary does not start on this machine"

mkdir -p "$dir" 2>/dev/null || fail "cannot create $dir (use --dir, or XLANG_INSTALL_DIR, to pick another place)"
# Copy next to the target and rename, so a running `xlang` is replaced atomically.
cp "$tmp/xl" "$dir/.xl.new.$$" 2>/dev/null || fail "cannot write to $dir (use --dir, or XLANG_INSTALL_DIR, to pick another place)"
mv -f "$dir/.xl.new.$$" "$dir/xl"

printf '%sinstalled xlang %s%s -> %s/xlang\n' "$green" "$version" "$reset" "$dir" >&2

case ":$PATH:" in
  *":$dir:"*) ;;
  *)
    printf '\n%s is not on your PATH. Add it to your shell profile:\n\n  export PATH="%s:$PATH"\n\n' "$dir" "$dir" >&2 ;;
esac

# Tab completion for the shells in use (bash, zsh, fish). Never fails the install.
if [ "$completions" = "1" ]; then
  step "Setting up tab completion"
  "$dir/xl" completions install >&2 || note "could not set up completion; run 'xl completions install' later"
fi

note "update later with: xl self-update"
