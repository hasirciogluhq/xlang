#!/usr/bin/env bash
# Installs `xl`, the xlang tool, and then the compiler and stdlib with `xl lc install` (Linux and macOS, amd64 and arm64; Windows:
# scripts/install.ps1).
#
#   curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.sh | bash
#
# Options (or the matching environment variables):
#   --version <tag>     XL_VERSION          release of xl to install (default: the newest stable, else the newest pre-release)
#   --dir <path>        XL_INSTALL_DIR      where `xl` is placed (default: $HOME/.local/bin)
#   --no-toolchain      XL_NO_TOOLCHAIN=1   do not run `xl lc install` afterwards
#   -h, --help
#
# The archive `xl_<version>_<os>_<arch>.tar.gz` is checked against the release's checksums.txt, the new binary is run once, and only
# then moved into place (xlang-docs/compiler/install.md).
set -euo pipefail

REPO="hasirciogluhq/xlang"
version="${XL_VERSION:-}"
dir="${XL_INSTALL_DIR:-$HOME/.local/bin}"
toolchain=1
[ -n "${XL_NO_TOOLCHAIN:-}" ] && toolchain=0

if [ -t 2 ] && [ -z "${NO_COLOR:-}" ]; then
  bold=$'\033[1m'; red=$'\033[1;31m'; green=$'\033[1;32m'; cyan=$'\033[1;36m'; reset=$'\033[0m'
else
  bold=""; red=""; green=""; cyan=""; reset=""
fi
step() { printf '%s==>%s %s%s%s\n' "$cyan" "$reset" "$bold" "$*" "$reset" >&2; }
fail() { printf '%serror:%s %s\n' "$red" "$reset" "$*" >&2; exit 1; }

usage() {
  awk 'NR > 1 && /^#/ { print substr($0, 3) } NR > 1 && !/^#/ { exit }' "$0" >&2 2>/dev/null ||
    echo "usage: install.sh [--version <tag>] [--dir <path>] [--no-toolchain]" >&2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --version) [ "$#" -ge 2 ] || fail "--version needs a value"; version="$2"; shift 2 ;;
    --dir) [ "$#" -ge 2 ] || fail "--dir needs a value"; dir="$2"; shift 2 ;;
    --no-toolchain) toolchain=0; shift ;;
    -h | --help) usage; exit 0 ;;
    *) fail "unknown option '$1' (try --help)" ;;
  esac
done

case "$(uname -s)" in
  Darwin) os=darwin ;;
  Linux) os=linux ;;
  MINGW* | MSYS* | CYGWIN*) fail "on Windows run scripts/install.ps1 in PowerShell" ;;
  *) fail "unsupported operating system: $(uname -s)" ;;
esac
case "$(uname -m)" in
  arm64 | aarch64) arch=arm64 ;;
  x86_64 | amd64) arch=amd64 ;;
  *) fail "unsupported CPU: $(uname -m)" ;;
esac
# A shell under Rosetta reports x86_64 on Apple silicon; install the native build.
if [ "$os" = darwin ] && [ "$arch" = amd64 ] && [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = 1 ]; then arch=arm64; fi

command -v curl >/dev/null 2>&1 || fail "curl is required"
command -v tar >/dev/null 2>&1 || fail "tar is required"
if command -v sha256sum >/dev/null 2>&1; then sha256() { sha256sum "$1" | awk '{ print $1 }'; }
elif command -v shasum >/dev/null 2>&1; then sha256() { shasum -a 256 "$1" | awk '{ print $1 }'; }
else fail "sha256sum or shasum is required to verify the download"; fi

api() { curl -fsSL --max-time 20 -H 'Accept: application/vnd.github+json' "https://api.github.com/repos/$REPO/$1"; }
# The first "tag_name" of a GitHub API answer (the list is newest first).
first_tag() { awk -F'"' '/"tag_name"/ { print $4; exit }'; }

if [ -z "$version" ]; then
  step "Looking up the newest release"
  # `releases/latest` never returns a pre-release and answers 404 while there is none.
  tag=$(api releases/latest 2>/dev/null | first_tag || true)
  [ -n "$tag" ] || tag=$(api 'releases?per_page=20' | first_tag || true)
  [ -n "$tag" ] || fail "could not find a release of $REPO (none is published yet, or the network is unreachable)"
else
  tag="v${version#v}"
fi
version="${tag#v}"

asset="xl_${version}_${os}_${arch}.tar.gz"
base="https://github.com/$REPO/releases/download/$tag"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

step "Downloading $asset"
curl -fSL --progress-bar -o "$tmp/$asset" "$base/$asset" || fail "download failed: $base/$asset"
curl -fsSL -o "$tmp/checksums.txt" "$base/checksums.txt" || fail "download failed: $base/checksums.txt"

want=$(awk -v a="$asset" '$2 == a || $2 == "*" a { print tolower($1); exit }' "$tmp/checksums.txt")
[ -n "$want" ] || fail "checksums.txt has no entry for $asset; refusing to install an unverified binary"
have=$(sha256 "$tmp/$asset")
[ "$have" = "$want" ] || fail "checksum mismatch for $asset (expected $want, got $have)"

tar -xzf "$tmp/$asset" -C "$tmp" || fail "could not unpack $asset"
[ -f "$tmp/xl" ] || fail "the archive has no xl binary"
chmod 0755 "$tmp/xl"
"$tmp/xl" version >/dev/null 2>&1 || fail "the downloaded xl does not start on this machine"

step "Installing to $dir"
mkdir -p "$dir" 2>/dev/null || fail "cannot create $dir (pick another place with --dir or XL_INSTALL_DIR)"
# Copy next to the target and rename: a running xl is replaced atomically.
cp "$tmp/xl" "$dir/.xl.new.$$" 2>/dev/null || fail "cannot write to $dir (pick another place with --dir or XL_INSTALL_DIR)"
mv -f "$dir/.xl.new.$$" "$dir/xl"
printf '%sinstalled xl %s%s -> %s/xl\n' "$green" "$version" "$reset" "$dir" >&2

if [ "$toolchain" = 1 ]; then
  step "Installing the compiler and stdlib (xl lc install)"
  "$dir/xl" lc install || fail "xl is installed, but \`xl lc install\` failed; run it again later"
fi

case ":$PATH:" in
  *":$dir:"*) ;;
  *) printf '\n%s is not on your PATH. Add it to your shell profile:\n\n  export PATH="%s:$PATH"\n\n' "$dir" "$dir" >&2 ;;
esac
echo "update later with: xl self-update and xl lc update" >&2
