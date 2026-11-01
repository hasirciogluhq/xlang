# Installing the compiler

This page covers the prebuilt `xlang` compiler: the install script, `xlang self-update`, and what is supported. Installing packages with `xlang add` is a different topic: see [Installing and Uninstalling](INSTALLING.md).

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

The compiler itself runs on macOS and Linux only. On Windows, install WSL and run the install script inside it. The compiler can still build Windows executables there (`--target x86_64-pc-windows-msvc`); see [Linking](LINKING.md).

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

## Colored output

Diagnostics, the error and summary lines, `xlang test` and `xlang self-update` use ANSI colors when the output is a terminal. `NO_COLOR` (non-empty) turns them off. `--color auto|always|never` overrides both for the run, and it is also passed to the compiler processes that `xlang run` and `xlang test` start, through the `XLANG_COLOR` environment variable. Colors are never written to `--diag-log` files or to `--diag-format=json`.
