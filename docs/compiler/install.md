# Installing the compiler

This page covers the prebuilt `xlang` compiler: the install script, `xlang self-update`, and what is supported. Installing packages with `xlang package install` is a different topic: see [Packages]({{ site.baseurl }}/compiler/packages.html).

## Supported platforms

| Host | arm64 | amd64 |
|------|-------|-------|
| macOS | yes | yes |
| Linux | yes | yes |
| Windows | no native build | no native build |

Releases are built by the `release` workflow when a `v*` tag is pushed. Each release holds one archive per OS and CPU, `xlang_<version>_<os>_<arch>.tar.gz` (`os` is `darwin` or `linux`, `arch` is `arm64` or `amd64`), and a combined `checksums.txt`. Tags that contain `-` are pre-releases: the installer and `self-update` never pick them as "latest". The workflow refuses to build when the version declared in `xmake.lua` (`xlang_version`) differs from the tag, because that value is what `xlang --version` reports and what `self-update` compares.

## Install script (macOS and Linux)

```bash
curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.sh | bash
```

The script:

1. Detects the OS and CPU (`uname`). On Apple silicon it installs the arm64 build even when the shell runs under Rosetta.
2. Finds the latest release through the GitHub API, or uses the requested one.
3. Downloads the archive and `checksums.txt`, and stops when the SHA-256 differs or `checksums.txt` has no entry for the archive.
4. Runs the new binary with `--version`, and only then copies it next to the target and renames it into place, so a running `xlang` is replaced atomically.
5. Prints the `PATH` line to add when the install directory is not on `PATH`.

| Option | Environment variable | Meaning |
|--------|----------------------|---------|
| `--version <x.y.z>` | `XLANG_VERSION` | Release to install. Default: the latest release. |
| `--dir <path>` | `XLANG_INSTALL_DIR` | Install directory. Default: `$HOME/.local/bin`. |

Pass options through a pipe with `bash -s --`, for example `curl -fsSL <url> | bash -s -- --version 0.1.0`. The script needs `curl`, `tar` and `sha256sum` or `shasum`.

Homebrew: `brew install hasirciogluhq/tap/xlang`. The release workflow updates the tap for every non-pre-release tag.

## Windows

The compiler itself runs on macOS and Linux only. On Windows, install WSL and run the install script inside it. The compiler can still build Windows executables there (`--target x86_64-pc-windows-msvc`); see [Linking]({{ site.baseurl }}/compiler/linking.html).

Target support from another host, as of the current development tree:

- `x86_64-pc-windows-msvc` builds end to end; `std/sys`, `std/net`, `std/tls` and `std/io` have Windows branches (`ntdll`/`kernel32`/`ws2_32`/`bcrypt`), and the entry stub is x86-64 assembly.
- `aarch64-pc-windows-msvc` does **not** build: the compiler emits an x86-64 entry stub for every Windows target, so even an empty `@entry` function fails under the aarch64 assembler. Recorded in [known limitations](../known-limitations.html).
- Building the compiler itself for Windows from source is not supported yet: several host-side source files still call POSIX process and path functions. Nothing in this repository has been compiled with MSVC.

## Updating: `xlang self-update`

```bash
xlang self-update           # aliases: update, upgrade
xlang self-update --check   # report only, install nothing
xlang self-update --force   # reinstall the latest release even when current
```

The command asks GitHub for the latest release and compares it with the running version (`xlang --version`). When the installed version is the latest or newer, it prints `xlang <version> is up to date` and exits with 0. Otherwise it downloads the archive for the host OS and CPU, verifies it against `checksums.txt`, runs the new binary with `--version`, and replaces the running executable in place. A failed download, a checksum mismatch or a binary that does not start leaves the installed compiler untouched.

- A compiler installed by Homebrew (its path contains `/Cellar/`) is not replaced; the command says to run `brew upgrade xlang`.
- When the directory of the running binary is not writable, the command fails with the path it could not write; rerun with enough permissions or reinstall with the script.
- The command needs `curl`, `tar` and `sha256sum` or `shasum` on `PATH`. It exits with 1 and an error when no release is published yet or the network is unreachable.
- The version comes from `set_version` in `xmake.lua`; a build from source reports that version, so it can be older than the latest release.

## Shell completion

`xlang completions <bash|zsh|fish>` prints a completion script on stdout. It is generated from the CLI's own command tree, so it always matches the installed version: subcommands and their aliases, every flag of each subcommand, the fixed values of options (`--emit`, `--color`, `--safety`, `-O`, ...), the shells for `completions` itself, and file names for the other positional arguments. It registers for both `xlang` and `xl`.

### Automatic setup: `xlang completions install`

Every install method sets completion up without a manual step, on every supported OS and CPU (the scripts do not depend on either; only the startup-file choice differs by OS):

| Method | What it does |
|--------|--------------|
| Install script | Runs `xlang completions install` after installing. Skip it with `--no-completions` or `XLANG_NO_COMPLETIONS=1`. A failure only prints a note. |
| `xlang self-update` | Runs `xlang completions install --refresh` with the new binary: rewrites the scripts that are already installed, never adds or re-adds a startup-file block. |
| Homebrew | The formula calls `generate_completions_from_executable(bin/"xlang", "completions", shells: [:bash, :zsh, :fish])`, so Homebrew installs the scripts itself. |
| Build from source | Run `xlang completions install` once. |

`install` sets a shell up when it is your login shell (`$SHELL`) or its startup file or config directory already exists, so it does not create files for shells you do not use:

| Shell | Script | Loaded by |
|-------|--------|-----------|
| fish | `$XDG_CONFIG_HOME/fish/completions/xlang.fish` and `xl.fish` (default `~/.config`) | fish autoloads it; no startup edit |
| zsh | `$XDG_DATA_HOME/xlang/completions/xlang.zsh` (default `~/.local/share`) | a block in `$ZDOTDIR/.zshrc` (default `~/.zshrc`); the script runs `compinit` itself when it has not run yet |
| bash | `$XDG_DATA_HOME/xlang/completions/xlang.bash` | a block in `~/.bash_profile` (macOS) or `~/.bashrc` (Linux) |

The block is marked `# >>> xlang completion >>>` ... `# <<< xlang completion <<<`, is added once (running `install` again changes nothing), and only sources the script when the file exists, so deleting the script is harmless. Delete the block to opt out; `self-update` will not add it back. The scripts are regenerated from the installed binary, so open a new terminal after an update to see new commands and flags.

Manual use without the setup: `source <(xlang completions bash)`, `source <(xlang completions zsh)` or `xlang completions fish | source`.

## Colored output

Diagnostics, the error and summary lines, `xlang test` and `xlang self-update` use ANSI colors when the output is a terminal. `NO_COLOR` (non-empty) turns them off. `--color auto|always|never` overrides both for the run, and it is also passed to the compiler processes that `xlang run` and `xlang test` start, through the `XLANG_COLOR` environment variable. Colors are never written to `--diag-log` files or to `--diag-format=json`.
