#!/usr/bin/env bash
# The xlang toolchain for CI from GitHub releases, never from the submodules (those are for local development only): the compiler
# `xlc` of hasirciogluhq/xlc and the stdlib of hasirciogluhq/xl-std.
#
#   toolchain.sh resolve <owner/repo> [tag]      prints the tag to use: the given one, else the newest stable release, else the
#                                                newest pre-release (the latest -rc.N)
#   toolchain.sh install <xlc tag> <std tag> <dir>
#                                                <dir>/bin/xlc (xlc.exe on Windows) and <dir>/std/index.xl, every archive checked
#                                                against its release's checksums.txt
#
# Assets: xlc_<version>_<os>_<arch>.tar.gz (.zip on Windows) with os darwin|linux|windows and arch amd64|arm64, and
# xl-std_<version>.tar.gz holding one xl-std_<version>/ directory (xlang-docs/compiler/install.md). Needs `gh` with GH_TOKEN.
set -euo pipefail

# A release tag: v1.2.3 with an optional -suffix. `gh api` prints the error body of a 404 on stdout, so anything else is no tag.
is_tag() { [[ "$1" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]]; }

resolve() {
  local repo=$1 tag=${2:-}
  if [ -z "$tag" ]; then
    # `releases/latest` never returns a pre-release or a draft and answers 404 while there is no stable release; the list is newest first.
    tag=$(gh api "repos/$repo/releases/latest" --jq .tag_name 2>/dev/null || true)
    is_tag "$tag" || tag=$(gh api "repos/$repo/releases?per_page=100" --jq '[.[] | select(.draft | not)] | first | .tag_name // empty')
  fi
  is_tag "$tag" || { echo "error: no usable release of $repo (got '$tag')" >&2; exit 1; }
  echo "$tag"
}

# fetch <repo> <tag> <asset> <dir>: downloads the asset and checksums.txt into <dir> and checks the SHA-256.
fetch() {
  local repo=$1 tag=$2 asset=$3 dir=$4 want got
  mkdir -p "$dir"
  gh release download "$tag" -R "$repo" -D "$dir" -p "$asset" -p checksums.txt --clobber
  want=$(awk -v a="$asset" '$2 == a || $2 == "*" a { print $1 }' "$dir/checksums.txt")
  [ -n "$want" ] || { echo "error: checksums.txt of $repo $tag has no entry for $asset" >&2; exit 1; }
  if command -v sha256sum >/dev/null 2>&1; then got=$(sha256sum "$dir/$asset" | awk '{ print $1 }')
  else got=$(shasum -a 256 "$dir/$asset" | awk '{ print $1 }'); fi
  # GNU sha256sum starts the line with `\` when the file name contains a backslash (a Windows path): that is not part of the hash.
  got=${got#\\}
  [ "$got" = "$want" ] || { echo "error: checksum mismatch for $asset (got $got, want $want)" >&2; exit 1; }
}

install() {
  local xlc_tag=$1 std_tag=$2 dest=$3 os arch ext=tar.gz exe=
  case "$(uname -s)" in
    Darwin) os=darwin ;;
    Linux) os=linux ;;
    MINGW* | MSYS* | CYGWIN*) os=windows; ext=zip; exe=.exe ;;
    *) echo "error: unsupported OS $(uname -s)" >&2; exit 1 ;;
  esac
  case "$(uname -m)" in
    x86_64 | amd64) arch=amd64 ;;
    arm64 | aarch64) arch=arm64 ;;
    *) echo "error: unsupported CPU $(uname -m)" >&2; exit 1 ;;
  esac
  rm -rf "$dest" && mkdir -p "$dest/bin"

  local asset="xlc_${xlc_tag#v}_${os}_${arch}.${ext}"
  fetch hasirciogluhq/xlc "$xlc_tag" "$asset" "$dest/download/xlc"
  if [ "$ext" = zip ]; then unzip -q -o "$dest/download/xlc/$asset" -d "$dest/bin"; else tar -xzf "$dest/download/xlc/$asset" -C "$dest/bin"; fi
  chmod +x "$dest/bin/xlc$exe"

  local inner="xl-std_${std_tag#v}"
  fetch hasirciogluhq/xl-std "$std_tag" "$inner.tar.gz" "$dest/download/std"
  tar -xzf "$dest/download/std/$inner.tar.gz" -C "$dest"
  mv "$dest/$inner" "$dest/std"
  [ -f "$dest/std/index.xl" ] || { echo "error: $inner.tar.gz has no $inner/index.xl" >&2; exit 1; }

  rm -rf "$dest/download"
  "$dest/bin/xlc$exe" --version
}

case "${1:-}" in
  resolve) shift; resolve "$@" ;;
  install) shift; install "$@" ;;
  *) echo "usage: toolchain.sh resolve <owner/repo> [tag] | install <xlc tag> <std tag> <dir>" >&2; exit 2 ;;
esac
