# xlang

**xlang** is an extremely low-level, LLVM-based programming language. Optional high-level APIs (`net`/`http`, and similar) are importable packages — not the language core. The `xlang` CLI compiles `.xlang` to native objects / executables / static libraries. C++ owns the compiler, host tooling, and LLVM codegen.

```
┌─────────────────┐     ┌──────────────────────────┐     ┌────────────────┐
│  .xlang source  │ ──► │  xlang (C++ / LLVM API)   │ ──► │  .o / exe / .a │
└─────────────────┘     └──────────────────────────┘     └────────────────┘
                               │
                CommonIrBuilder → Module → TargetMachine
                               │
          user declarations and linked objects
```

## Features

| Area | Support |
|------|---------|
| Types | `int32`, `int64`, `float`, `double`, `bool`, `string`, struct, pointer |
| Functions | Overload, variadic (`...`), `export` / `external` / `declare` |
| Modules | `import`, `import * as`, directory modules (`http/`), git packages (`import "github.com/user/repo"`) |
| Memory | `new` / `delete`, struct fields, heap |
| Control flow | `if` / `else`, `while` |
| Strings | Concat (`+`), `printf`-style formatted `print` |
| Concurrency | user-defined functions and linked objects |
| Native kernel | `declare syscall <n> name(...)` or `@syscall(n, args...)` |
| External symbols | plain `declare name(...)` → resolve from user-provided objects |

## Packages

A package is a git repository of plain `.xl` source, named by its repository path and versioned by its tags. There is no registry and no required layout: no monorepo or polyrepo rule, no `cmd/` or `apps/`, no standard the compiler enforces. Build your repositories the way you like; the package system only saves you the typing.

```sh
xlang package install github.com/user/repo      # newest stable tag (v1.2.0 or 1.2.0); repo@v1.2.0-test installs a test/pre-release tag when named
xlang run main.xl                               # from "github.com/user/repo" import ...
```

`xlang.toml` lists dependencies, `xlang.lock` pins their commits, `.xlang/cache` holds the sources. Builds never touch the network. The standard library is a package too: the compiler embeds a default and any project may use a local directory or a repository at another version. Packages may also ship prebuilt files per platform (`[[native]]`), which the compiler links. Details: **[docs/PACKAGE_MANAGER.md](docs/PACKAGE_MANAGER.md)**.

Full language reference: **[docs/LANGUAGE.md](docs/LANGUAGE.md)** · Linking: **[docs/LINKING.md](docs/LINKING.md)**  
VS Code extension: **[vscode/README.md](vscode/README.md)**

## Install

Prebuilt compilers exist for macOS and Linux on arm64 and amd64. The installer picks the archive for your OS and CPU, checks its SHA-256 against the release's `checksums.txt`, and installs the **latest release** by default. Releases are built by pushing a `v*` tag; until the first one exists the installer and `xlang self-update` report that no release is published.

### macOS and Linux

```bash
curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.sh | bash
```

`xlang` is placed in `~/.local/bin` (change it with `XLANG_INSTALL_DIR=<dir>`; pin a release with `XLANG_VERSION=<x.y.z>`). If that directory is not on your `PATH`, the installer prints the line to add to your shell profile. Check it with `xlang --version`.

With Homebrew: `brew install hasirciogluhq/tap/xlang`.

### Windows

There is no native Windows build of the compiler yet. Install [WSL](https://learn.microsoft.com/windows/wsl/install) (`wsl --install`), then run the Linux command above inside it. The compiler can still produce Windows executables from there (`--target x86_64-pc-windows-msvc`).

### Update

```bash
xlang self-update           # also: xlang update, xlang upgrade
xlang self-update --check   # only report whether a newer release exists
```

`self-update` downloads the latest release for your OS and CPU, verifies it, and replaces the running binary; when you already have the latest it says so. A Homebrew install is updated with `brew upgrade xlang` instead. Details: **[docs/INSTALL.md](docs/INSTALL.md)**.

### Shell completion

Tab completion for bash, zsh and fish is set up **automatically** by every install method: the install script and `xlang completions install` configure the shells you use (the login shell, or any shell whose startup file already exists), and the Homebrew formula installs the scripts into Homebrew's completion directories. It covers subcommands, flags and option values (`--emit`, `--color`, ...) for both `xlang` and `xl`, and `xlang self-update` refreshes the scripts. Open a new terminal afterwards.

```bash
xlang completions install            # (re)run the setup by hand, e.g. after a build from source
xlang completions zsh                # print a script instead: bash | zsh | fish
curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.sh | bash -s -- --no-completions
```

The setup writes `~/.local/share/xlang/completions/xlang.{bash,zsh}` and adds one marked block (`# >>> xlang completion >>>`) to `~/.zshrc` and `~/.bash_profile` (macOS) or `~/.bashrc` (Linux); fish gets `~/.config/fish/completions/{xlang,xl}.fish`. Delete the block to opt out.

## Requirements (building from source)

- [xmake](https://xmake.io) ≥ 2.8
- **C++26** compiler (Clang or GCC); C++23 is the minimum, and the build picks C++26 automatically when available
- **Clang** on `PATH` (used when linking the final image)
- LLVM (via xmake package)

## Build

```bash
xmake
# optional: xmake f -m debug && xmake
```

Compiler binary: `./outputs/xlang`

xmake builds the C/C++ compiler and host tooling only.

## Quick start

```bash
./outputs/xlang run examples/strings.xl
./outputs/xlang run examples/hello.xl
./outputs/xlang run examples/types.xl
./outputs/xlang run examples/scheduler.xl
./outputs/xlang run examples/pointers.xl
./outputs/xlang run examples/filesystem.xl
./outputs/xlang run examples/http_server.xl
```

## CLI

Defaults for `xlang build foo.xlang`: executable + OS baseline syslibs (e.g. `-pthread`).

### `xlang build`

```bash
xlang build app.xlang                              # executable (default)
xlang build app.xlang lib.o -o myapp
xlang build app.xlang --build=object -o app.o
xlang build app.xlang --build=static -o libapp.a
xlang build app.xlang --build=shared               # experimental
xlang build app.xlang --target linux --arch x64
xlang build app.xlang --emit-ir                    # dump Module IR
xlang build app.xlang --keep-ir
```

`--build=` values: `executable` (aliases: `binary`, `exe`) | `static` | `shared` | `object` (legacy alias: `lib` → object).

### `xlang run`

```bash
xlang run program.xlang
xlang run main.xlang lib.o
xlang run program.xlang --keep-artifacts
```

### `xlang parse` / `xlang test`

```bash
xlang parse examples/hello.xlang
xlang test                         # default: test/xlang/
xlang test http
xlang test --parallel
```

## Declares (two paths)

```xlang
declare external_fn(host: string, port: int32): int64   // external C ABI
declare syscall 1 write(fd: int64, buf: int64, n: int64): int64 // CPU-native trap
// or: @syscall(1, fd, buf, n)
```


## Tree

```
xlang/
├── src/
│   ├── cli/                 # main
│   ├── lang/                # ast, lexer, parser, types
│   ├── codegen/             # CommonIrBuilder, TargetMachine, lowering
│   ├── compiler/            # compile / link / module / test
│   ├── host/                # layout and target resolution
│   ├── platform/            # linux | macosx | windows
│   ├── util/
├── include/xlang/
├── examples/
├── test/xlang/
├── vscode/
├── xmake.lua
├── xmake/
└── docs/
```

## Architecture

1. **Lexer / parser** (`src/lang`) → AST  
2. **Module loader** — `import` merge  
3. **Codegen** (`src/codegen`) — AST → `CommonIrBuilder` → `llvm::Module` → `TargetMachine` → `.o`  
4. **Link** — generated object + user-provided objects + OS baseline syslibs

Kernel entry is only via `declare syscall` / `@syscall`; ordinary `declare` names are external symbols.

## Environment

| Variable | Description |
|----------|-------------|
| `XLANG_MODULE_PATH` | Module search directories (path list separator) |
| `XLANG_LIB_PATH` | Static library (`.a`) search directories |
| `XLANG_CLANG` | Clang binary override |
| `XLANG_TARGET_OS` / `XLANG_TARGET_ARCH` / `XLANG_TARGET_TRIPLE` | Target overrides |

## Status

Early-stage (v0.1). APIs and syntax may change. See [docs/LANGUAGE.md](docs/LANGUAGE.md) limitations.

## License

To be updated according to the project owner's license preference.
