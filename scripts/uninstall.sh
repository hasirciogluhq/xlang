#!/usr/bin/env bash
# Removes xlang from this machine (Linux and macOS; Windows: scripts/uninstall.ps1): every file that any version of `xl`, the
# older `xlang` binary, its Homebrew formula, `xl lc` or the VS Code extension put in a well-known place.
#
#   curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/development/scripts/uninstall.sh | bash
#
# Unlike the install scripts this one is not a release asset: it lives on the `development` branch and is used from there, so it
# always knows every layout up to the newest. It scans, prints a numbered list, and asks which entries to remove (`all`, numbers
# and ranges such as `1 3 5-7`, or nothing). The answer is read from the terminal, so it works through a pipe.
#
# Options (or the matching environment variables):
#   --yes        XL_UNINSTALL_YES=1   remove everything found without asking
#   --list                            only print what was found
#   -h, --help
#
# What it looks for (xlang-docs/compiler/install.md "Uninstall"):
#   xl, xlang, xlc   in $XL_INSTALL_DIR, $XLANG_INSTALL_DIR, ~/.local/bin, ~/bin, /usr/local/bin and every $PATH directory;
#                    a file counts only when its bytes carry the xlang marker (`hasirciogluhq` for xl/xlang, `_xl_initAll` for
#                    xlc), so Xen's `xl` and other tools of the same name are never listed. Half-written `.xl.new.*` files too.
#   $XLANG_HOME      the compilers and stdlibs of `xl lc` and `xl std` (default ~/.xlang)
#   Homebrew         the `xlang` formula and the `hasirciogluhq/tap` tap of the old releases
#   completion       `xlang completions install` of the old releases: $XDG_DATA_HOME/xlang, the fish scripts xl.fish and
#                    xlang.fish, and the `# >>> xlang completion >>>` block in ~/.zshrc, ~/.bashrc and ~/.bash_profile
#   editors          the `xlang.xlang` extension of VS Code, VS Code Insiders, VSCodium and Cursor
# A project's own `.xlang/cache` belongs to the project and is left alone (`xl pkg clean` empties it).
set -euo pipefail

yes=0
list=0
[ -n "${XL_UNINSTALL_YES:-}" ] && yes=1

if [ -t 2 ] && [ -z "${NO_COLOR:-}" ]; then
  bold=$'\033[1m'; dim=$'\033[2m'; red=$'\033[1;31m'; green=$'\033[1;32m'; cyan=$'\033[1;36m'; reset=$'\033[0m'
else
  bold=""; dim=""; red=""; green=""; cyan=""; reset=""
fi
step() { printf '%s==>%s %s%s%s\n' "$cyan" "$reset" "$bold" "$*" "$reset" >&2; }
warn() { printf '%swarning:%s %s\n' "$red" "$reset" "$*" >&2; }
fail() { printf '%serror:%s %s\n' "$red" "$reset" "$*" >&2; exit 1; }

usage() {
  awk 'NR > 1 && /^#/ { print substr($0, 3) } NR > 1 && !/^#/ { exit }' "$0" >&2 2>/dev/null ||
    echo "usage: uninstall.sh [--yes] [--list]" >&2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --yes | -y) yes=1; shift ;;
    --list) list=1; shift ;;
    -h | --help) usage; exit 0 ;;
    *) fail "unknown option '$1' (try --help)" ;;
  esac
done

case "$(uname -s)" in
  Darwin) os=darwin ;;
  Linux) os=linux ;;
  MINGW* | MSYS* | CYGWIN*) fail "on Windows run scripts/uninstall.ps1 in PowerShell" ;;
  *) fail "unsupported operating system: $(uname -s)" ;;
esac

# The found entries: kind (file, dir, block, brew, tap, ext), target, and the text shown in the list.
kinds=(); targets=(); labels=()
seen=$'\n'
add() {
  case "$seen" in *$'\n'"$1:$2"$'\n'*) return 0 ;; esac
  seen+="$1:$2"$'\n'
  kinds+=("$1"); targets+=("$2"); labels+=("$3")
}
size() { du -sh "$1" 2>/dev/null | awk '{ print $1 }'; }
# The directory part with symlinks resolved, so one binary reached through two PATH entries is listed once.
canon() { printf '%s/%s' "$(cd "$(dirname "$1")" 2>/dev/null && pwd -P)" "$(basename "$1")"; }

xhome="${XLANG_HOME:-$HOME/.xlang}"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
config="${XDG_CONFIG_HOME:-$HOME/.config}"

step "Looking for xlang"

# Binaries. A Homebrew link (into .../Cellar/xlang/...) is the formula's and goes with it.
dirs=("${XL_INSTALL_DIR:-}" "${XLANG_INSTALL_DIR:-}" "$HOME/.local/bin" "$HOME/bin" /usr/local/bin)
IFS=: read -r -a pathDirs <<<"${PATH:-}"
dirs+=("${pathDirs[@]}")
for d in "${dirs[@]}"; do
  [ -n "$d" ] && [ -d "$d" ] || continue
  for name in xl xlang xlc; do
    f="$d/$name"
    [ -e "$f" ] || [ -L "$f" ] || continue
    if [ -L "$f" ]; then
      case "$(readlink "$f")" in *Cellar/xlang/*) continue ;; esac
    fi
    case "$(canon "$f")" in "$(canon "$xhome")"/*) continue ;; esac
    marker=hasirciogluhq
    [ "$name" = xlc ] && marker=_xl_initAll
    # A dangling link left by a removed build still points at an xlang path.
    if [ -L "$f" ] && [ ! -e "$f" ]; then
      case "$(readlink "$f")" in *xlang* | */xl | */xlc) add file "$(canon "$f")" "$f  ${dim}(broken link)${reset}" ;; esac
      continue
    fi
    [ -f "$f" ] && grep -aqF "$marker" "$f" 2>/dev/null || continue
    version=$("$f" "$([ "$name" = xl ] && echo version || echo --version)" 2>/dev/null | head -1 || true)
    add file "$(canon "$f")" "$f  ${dim}(${version:-unknown version}$([ -L "$f" ] && echo ", link"))${reset}"
  done
  for f in "$d"/.xl.new.* "$d"/.xlang.new.*; do
    [ -e "$f" ] && add file "$f" "$f  ${dim}(left by an interrupted install)${reset}"
  done
done

# The toolchain of `xl lc` (src/lc/toolchain.xl).
for h in "$xhome" "$HOME/.xlang"; do
  [ -d "$h" ] || continue
  installed=$(cd "$h" && ls -d xlc/* std/* 2>/dev/null | paste -sd ' ' -)
  add dir "$h" "$h  ${dim}($(size "$h"); ${installed:-no releases})${reset}"
done

# Homebrew: the old releases shipped `brew install hasirciogluhq/tap/xlang`.
if command -v brew >/dev/null 2>&1; then
  if brew list --formula --versions xlang >/dev/null 2>&1; then
    add brew xlang "Homebrew formula  ${dim}($(brew list --formula --versions xlang 2>/dev/null))${reset}"
  fi
  if brew tap 2>/dev/null | grep -qx 'hasirciogluhq/tap'; then
    add tap hasirciogluhq/tap "Homebrew tap hasirciogluhq/tap"
  fi
fi

# Shell completion of the old `xlang completions install`.
[ -d "$data/xlang" ] && add dir "$data/xlang" "$data/xlang  ${dim}(completion scripts)${reset}"
for f in "$config/fish/completions/xl.fish" "$config/fish/completions/xlang.fish"; do
  [ -f "$f" ] && grep -qF 'xlang' "$f" && add file "$f" "$f  ${dim}(fish completion)${reset}"
done
for f in "${ZDOTDIR:-$HOME}/.zshrc" "$HOME/.zshrc" "$HOME/.bashrc" "$HOME/.bash_profile"; do
  [ -f "$f" ] && grep -qF '# >>> xlang completion >>>' "$f" && add block "$f" "$f  ${dim}(the xlang completion block)${reset}"
done

# Editor extensions: <editor dir>/extensions/xlang.xlang-<version>. The editor's CLI removes it from its own index as well.
for e in ".vscode:code" ".vscode-insiders:code-insiders" ".vscode-oss:codium" ".cursor:cursor"; do
  for f in "$HOME/${e%%:*}"/extensions/xlang.xlang-*; do
    [ -d "$f" ] && add ext "${e#*:} $f" "$f  ${dim}(editor extension)${reset}"
  done
done

if [ "${#kinds[@]}" -eq 0 ]; then
  printf '%snothing of xlang was found%s\n' "$green" "$reset" >&2
  exit 0
fi

printf '\n' >&2
for i in "${!kinds[@]}"; do printf '  %s%2d%s  %s\n' "$bold" "$((i + 1))" "$reset" "${labels[$i]}" >&2; done
printf '\n' >&2
[ "$list" = 1 ] && exit 0

# Which entries: all of them with --yes, else the answer read from the terminal (stdin is the script when piped from curl).
picked=()
if [ "$yes" = 1 ]; then
  picked=("${!kinds[@]}")
else
  [ -r /dev/tty ] || fail "no terminal to ask on; pass --yes to remove everything, or --list to only print it"
  printf 'Remove which? [all, numbers like 1 3 5-7, empty for none]: ' >&2
  read -r answer </dev/tty || answer=""
  for word in ${answer//,/ }; do
    case "$word" in
      all | a) picked=("${!kinds[@]}") ;;
      *-*) lo=${word%-*}; hi=${word#*-}
        [[ "$lo" =~ ^[0-9]+$ && "$hi" =~ ^[0-9]+$ ]] || fail "'$word' is not a number or a range"
        for ((n = lo; n <= hi; n++)); do picked+=("$((n - 1))"); done ;;
      *) [[ "$word" =~ ^[0-9]+$ ]] || fail "'$word' is not a number or a range"
        picked+=("$((word - 1))") ;;
    esac
  done
  [ "${#picked[@]}" -gt 0 ] || { echo "nothing removed" >&2; exit 0; }
fi

# rm, and through sudo for a system directory such as /usr/local/bin when a terminal can ask for the password.
remove() {
  rm -rf "$1" 2>/dev/null && return 0
  if command -v sudo >/dev/null 2>&1 && [ -r /dev/tty ]; then sudo rm -rf "$1" </dev/tty && return 0; fi
  return 1
}

failed=0
handled=$'\n'
for i in "${picked[@]}"; do
  [ "$i" -ge 0 ] && [ "$i" -lt "${#kinds[@]}" ] || { warn "there is no entry $((i + 1))"; failed=1; continue; }
  case "$handled" in *$'\n'"$i"$'\n'*) continue ;; esac
  handled+="$i"$'\n'
  kind=${kinds[$i]}; target=${targets[$i]}
  ok=1
  case "$kind" in
    file | dir) remove "$target" || ok=0 ;;
    block)
      # Drop the lines from the begin marker to the end marker; `cat >` keeps the file's owner, mode and any symlink to it.
      tmp=$(mktemp)
      awk '/^# >>> xlang completion >>>/ { skip = 1 } !skip { print } /^# <<< xlang completion <<</ { skip = 0 }' "$target" >"$tmp" &&
        cat "$tmp" >"$target" || ok=0
      rm -f "$tmp" ;;
    brew) brew uninstall --formula xlang >&2 || ok=0 ;;
    tap) brew untap hasirciogluhq/tap >&2 || ok=0 ;;
    ext)
      cli=${target%% *}; dir=${target#* }
      if command -v "$cli" >/dev/null 2>&1; then "$cli" --uninstall-extension xlang.xlang >/dev/null 2>&1 || true; fi
      [ ! -e "$dir" ] || remove "$dir" || ok=0 ;;
  esac
  if [ "$ok" = 1 ]; then
    printf '%sremoved%s %s\n' "$green" "$reset" "${labels[$i]}" >&2
  else
    warn "could not remove ${labels[$i]}"
    failed=1
  fi
done

hash -r 2>/dev/null || true
[ "$failed" = 0 ] || exit 1
echo "open a new terminal so the shell forgets the removed commands and completion" >&2
