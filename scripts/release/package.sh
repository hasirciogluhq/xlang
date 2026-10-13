#!/usr/bin/env bash
# Package the built xlang compiler into a release archive.
#
# Usage: scripts/release/package.sh <version> <os> <arch> [binary]
#   version  release version without the leading "v" (e.g. 0.1.0)
#   os       darwin | linux
#   arch     amd64 | arm64
#   binary   path to the built compiler (default: build/xlang)
#
# Output (in ./dist):
#   xlang_<version>_<os>_<arch>.tar.gz
#   xlang_<version>_<os>_<arch>.tar.gz.sha256
set -euo pipefail

if [ "$#" -lt 3 ]; then
  echo "usage: $0 <version> <os> <arch> [binary]" >&2
  exit 2
fi

version="$1"
os="$2"
arch="$3"
binary="${4:-build/xlang}"

case "$os" in
  darwin | linux) ;;
  *) echo "error: unsupported os '$os' (expected darwin|linux)" >&2; exit 2 ;;
esac
case "$arch" in
  amd64 | arm64) ;;
  *) echo "error: unsupported arch '$arch' (expected amd64|arm64)" >&2; exit 2 ;;
esac

if [ ! -x "$binary" ]; then
  echo "error: compiler binary not found or not executable: $binary" >&2
  exit 1
fi

# Fail early if the binary cannot even start.
"$binary" --help >/dev/null

name="xlang_${version}_${os}_${arch}"
dist="dist"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

mkdir -p "$dist"
cp "$binary" "$stage/xlang"
chmod 0755 "$stage/xlang"

# Strip symbols to keep the archive small; non-fatal if strip is unavailable.
if [ "$os" = "darwin" ]; then
  strip -x "$stage/xlang" || true
else
  strip --strip-unneeded "$stage/xlang" || true
fi

archive="$dist/$name.tar.gz"
tar -C "$stage" -czf "$archive" xlang

if command -v sha256sum >/dev/null 2>&1; then
  hash="$(sha256sum "$archive" | awk '{print $1}')"
else
  hash="$(shasum -a 256 "$archive" | awk '{print $1}')"
fi
printf '%s  %s\n' "$hash" "$(basename "$archive")" >"$archive.sha256"

echo "packaged $archive"
echo "sha256   $hash"
