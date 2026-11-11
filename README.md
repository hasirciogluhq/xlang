# xlang

Documentation: <https://hasirciogluhq.github.io/xlang-docs/> (the `xlang-docs/` submodule, deployed from its own repository).

**xlang** is a low-level, LLVM-based programming language. There is no libc and no C runtime: the
compiler knows only the language, the CPU primitives and a closed list of builtins, and everything
else (printing, memory allocation, `memcpy`, threads, sockets, HTTP, TLS) is ordinary xlang code in
`xl-std/`. Optional high-level APIs are importable packages, not the language core.

Source files use the extension `.xl`. The CLI compiles them to native objects, executables, static
or shared libraries. C++ owns the compiler, host tooling and LLVM codegen.

```
┌──────────────┐     ┌───────────────────────────┐     ┌──────────────────────┐
│  .xl source  │ ──► │  xlang (C++ / LLVM API)   │ ──► │ .o / exe / .a / .so  │
└──────────────┘     └───────────────────────────┘     └──────────────────────┘
                                │
                    HIR → LLVM IR → TargetMachine
                                │
              user declarations and linked objects
```

## Features

| Area            | Support                                                                                                                                                                   |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Types           | `i8`–`i64`, `ui8`–`ui64`, `isize`/`usize`, `f32`/`f64`, `bool`, `void`, `*T`, `fn(A)->R`, `[T; N]`, `str`, `(A, B)` tuple                                                 |
| User types      | `struct`, `class` (single inheritance, vptr), `interface` (fat pointer), `enum` (value and payload, including generic payload enums such as `Result<T, E>`), `type` alias              |
| Generics        | Functions, structs, classes, interfaces, enums (payload enums included), aliases and class methods; monomorphized, no constraints                                          |
| Functions       | Overload by parameter types, parameter packs (`..args`), `fn alias = target`, `@entry` / `@init` / `@fini`                                                                |
| Control flow    | `if`/`else`, `while`, `do`/`while`, three-clause `for`, `for i in a..b` / `a..=b`, `switch` (no fallthrough), labeled `break`/`continue`, `defer`, `try`/`catch`, ternary |
| Memory          | `new T`, `new(p) T`, `delete p`; the allocator is a registered stdlib function, not a builtin                                                                             |
| Safety          | `*T` unchecked; array indexing bounds-checked by default (`--safety=checked`, off with `unchecked`)                                                                       |
| Modules         | `import` / `from ... import`, `import *`, `export`, `export * from` re-exports; `.xl` or `index.xl` per directory                                                         |
| Packages        | git repositories of `.xl` source; `xlang.toml`, `xlang.lock`, `.xlang/cache`; no registry, no required layout                                                             |
| Atomics and asm | `__builtin_atomic*`, `__builtin_fence`, `__builtin_asm`                                                                                                                   |
| Native kernel   | `syscall(nr, args...)` builtin, or `@library("name") declare fn` for OS runtime libraries                                                                                 |
| Diagnostics     | Coded errors and warnings (`E`/`W`), `--allow`/`--deny`/`@allow`, `--explain CODE`                                                                                        |

## Packages

A package is a git repository of plain `.xl` source, named by its repository path and versioned by
its tags. There is no registry and no required layout: no monorepo or polyrepo rule, no `cmd/` or
`apps/`, no standard the compiler enforces. Build your repositories the way you like; the package
system only saves you the typing.

```sh
xlang package install github.com/user/repo      # newest stable tag (v1.2.0 or 1.2.0); repo@v1.2.0-test installs a pre-release tag when named
xlang run --incremental main.xl                 # from "github.com/user/repo" import ...
```

`xlang.toml` lists dependencies, `xlang.lock` pins their commits, `.xlang/cache` holds the sources.
Builds never touch the network. The standard library is a package too: the compiler embeds a default
and any project may use a local directory or a repository at another version (`--std`, `[std]` in
`xlang.toml`). Packages may also ship prebuilt files per platform (`[[native]]`), which the compiler
links. Details: **[xlang-docs/compiler/packages.md](xlang-docs/compiler/packages.md)**.

Full language reference: **[xlang-docs/language/index.md](xlang-docs/language/index.md)** · Linking: **[xlang-docs/compiler/linking.md](xlang-docs/compiler/linking.md)** · Runtime: **[xlang-docs/compiler/runtime.md](xlang-docs/compiler/runtime.md)**  
Known gaps and open decisions: **[xlang-docs/known-limitations.md](xlang-docs/known-limitations.md)**  
VS Code extension: **[vscode/README.md](vscode/README.md)**

## Install

Prebuilt compilers exist for macOS and Linux on arm64 and amd64. The installer picks the archive for
your OS and CPU, checks its SHA-256 against the release's `checksums.txt`, and installs the **latest
release** by default. Releases are built by pushing a `v*` tag; until the first one exists the
installer and `xlang self-update` report that no release is published.

### macOS and Linux

```bash
curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.sh | bash
```

`xlang` is placed in `~/.local/bin` (change it with `XLANG_INSTALL_DIR=<dir>`; pin a release with
`XLANG_VERSION=<x.y.z>`). If that directory is not on your `PATH`, the installer prints the line to
add to your shell profile. Check it with `xlang --version`.

With Homebrew: `brew install hasirciogluhq/tap/xlang`.

### Windows

There is no native Windows build of the compiler yet. Install
[WSL](https://learn.microsoft.com/windows/wsl/install) (`wsl --install`), then run the Linux command
above inside it. The compiler can still produce Windows executables from there
(`--target x86_64-pc-windows-msvc`).

### Update

```bash
xlang self-update           # also: xlang update, xlang upgrade
xlang self-update --check   # only report whether a newer release exists
```

`self-update` downloads the latest release for your OS and CPU, verifies it, and replaces the running
binary; when you already have the latest it says so. A Homebrew install is updated with
`brew upgrade xlang` instead. Details: **[xlang-docs/compiler/install.md](xlang-docs/compiler/install.md)**.

### Shell completion

Tab completion for bash, zsh and fish is set up **automatically** by every install method: the install
script and `xlang completions install` configure the shells you use (the login shell, or any shell
whose startup file already exists), and the Homebrew formula installs the scripts into Homebrew's
completion directories. It covers subcommands, flags and option values (`--emit`, `--color`, ...) for
both `xlang` and `xl`, and `xlang self-update` refreshes the scripts. Open a new terminal afterwards.

```bash
xlang completions install            # (re)run the setup by hand, e.g. after a build from source
xlang completions zsh                # print a script instead: bash | zsh | fish
curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.sh | bash -s -- --no-completions
```

The setup writes `~/.local/share/xlang/completions/xlang.{bash,zsh}` and adds one marked block
(`# >>> xlang completion >>>`) to `~/.zshrc` and `~/.bash_profile` (macOS) or `~/.bashrc` (Linux);
fish gets `~/.config/fish/completions/{xlang,xl}.fish`. Delete the block to opt out.

## Requirements (building from source)

- [xmake](https://xmake.io) ≥ 2.8
- **C++26** compiler (Clang or GCC); C++23 is the minimum, and the build picks C++26 automatically
  when available
- **Clang** on `PATH` (used when linking the final image)
- LLVM via `llvm-config` (or `$LLVM_CONFIG`), linked statically: passes, the aarch64/x86 backends,
  and the object/archive writers

## Build

```bash
xmake
# optional: xmake f -m debug && xmake
```

Compiler binary: `./outputs/xlang`. xmake builds the C/C++ compiler and host tooling only.

Useful targets:

```bash
xmake build std           # type-check the standard library with the fresh compiler
xmake run e2e             # end-to-end programs in xl-std/test/e2e, built with $XLC (default xlc); Docker and CrossOver legs: xl-std/test/e2e/run.sh
```

While working, run only the tests of what changed; the full suites take over a minute.

## Quick start

A program is a `.xl` file with an `@entry` function:

```xlang
from "std" import print

struct Point { x: i32, y: i32 }

fn divMod(a: i64, b: i64) -> (i64, i64) {
    return (a / b, a % b)
}

@entry fn main() -> i32 {
    local p: Point = Point { x: 1, y: 2 }
    local (q, r) = divMod(17, 5)
    print("point sum: ")
    print((p.x + p.y) as i64)
    print("\n")
    print("17 / 5 = ")
    print(q)
    print(" remainder ")
    print(r)
    print("\n")
    return 0
}
```

```bash
./outputs/xlang run hello.xl
./outputs/xlang run xl-std/test/e2e/strings.xl
./outputs/xlang run xl-std/test/e2e/for.xl
```

## CLI

Defaults for `xlang build foo.xl`: executable + OS baseline syslibs (e.g. `-pthread`).

### `xlang build`

```bash
xlang build app.xl                                 # executable (default)
xlang build app.xl lib.o -o myapp
xlang build app.xl --emit=obj -o app.o
xlang build app.xl --emit=static -o libapp.a
xlang build app.xl --emit=shared -o libapp.so
xlang build app.xl --emit=ir                       # dump LLVM IR
xlang build app.xl --emit=bc --emit=header          # bitcode or C header
xlang build app.xl --target aarch64-apple-darwin
xlang build app.xl -O2 -g --cpu generic
xlang build app.xl --incremental --cache-dir .cache  # one object per module, reused when unchanged
```

`--emit=` values: `exe` (default) | `obj` | `ir` | `bc` | `header` | `static` | `shared`. The
compiler runs the full pipeline by default and links only its own output.

Other flags: `-D NAME[=VALUE]`, `-U NAME`, `--mangle=xl|none`, `--safety=checked|unchecked`,
`--no-std`, `--std=<dir|host|user|repo[@tag]>`, `--std-features`, `--layout-scheme`,
`--layout-history`, `--allow`/`--deny`/`--warn-all`/`--allow-all`, `--context N`, `--print-cfg`.

### Other subcommands

```bash
xlang check xl-std/test/e2e/print.xl          # parse + module checks (types, enums, layouts, staticAssert)
xlang parse foo.xl                     # preprocess, expand macros, parse; print the AST summary
xlang layout foo.xl                    # print offsets/sizes/alignment of structs and classes
xlang run foo.xl                       # build into a temp file, run it, exit with its status
xlang test xl-std/test/unit/                  # build and run `*.test.xl` (file, dir, glob, re:<regex>)
xlang package install|update|remove|list|clean
xlang completions bash|zsh|fish|install
xlang self-update [--check]
xlang --explain E0709                  # long explanation of a diagnostic code
```

## Declares (two paths)

```xlang
@library("m") declare fn cbrt(x: f64) -> f64     // an OS runtime library
declare fn host_fn(a: i32) -> i32                // an external C ABI symbol
syscall(1, fd, buf, n)                           // the kernel trap builtin
```

User code never calls OS runtime libraries directly: a stdlib area declares and calls them behind
`#if os` where the OS differs, and user programs reach them only through stdlib functions. The one
exception is an explicit `declare` in the user's own program, which is the supported extension point.

## Tree

```
xlang/
├── src/
│   ├── cli/                 # the command tree, run/test runner, package, update, completions
│   ├── lang/                # lexer, preprocess, macro, parser, ast, types, config
│   ├── sema/                # per-module semantic services and the lowering pass
│   ├── hir/                 # typed HIR, pruning
│   ├── codegen/             # HIR → LLVM IR, emit (obj/ir/bc/header/static/shared)
│   ├── driver/              # build orchestration, linking
│   ├── pkg/                 # xlang.toml, semver, git, cache
│   └── util/                # source manager, diagnostics, errors
├── include/xlang/           # public headers
├── xl-std/                  # the standard library, written in xlang (git submodule)
│   └── test/                # e2e programs (run across OSes) and `*.test.xl` unit suites
├── xlang-docs/                    # the published documentation site
├── xmake.lua
├── xmake/
└── vscode/
```

See **[src/README.md](src/README.md)** for the per-file layout.

## Architecture

1. **Lexer / preprocess / macro / parser** (`src/lang`) → AST
2. **Per-module sema** (`src/sema/sema`): name resolution, types, layouts, constants, generic instances
3. **Lowering** (`src/sema/lower`): scopes, drop glue, `defer`, `try`, desugaring → typed HIR (`src/hir`)
4. **Codegen** (`src/codegen`): HIR → LLVM IR → `TargetMachine` → `.o`, or IR / bitcode / C header
5. **Link** (`src/driver`): the generated object plus user objects, OS runtime libraries and packages

Entry symbols (`_start`, `mainCRTStartup`) and `_xl_initAll` are compiler-owned; the allocator,
`panic`, `free` and `unwind` are stdlib functions reached through `register(role)`.

## Environment

| Variable                                                        | Description                                     |
| --------------------------------------------------------------- | ----------------------------------------------- |
| `XLANG_MODULE_PATH`                                             | Module search directories (path list separator) |
| `XLANG_LIB_PATH`                                                | Static library (`.a`) search directories        |
| `XLANG_CLANG`                                                   | Clang binary override                           |
| `XLANG_TARGET_OS` / `XLANG_TARGET_ARCH` / `XLANG_TARGET_TRIPLE` | Target overrides                                |
| `LLVM_CONFIG`                                                   | The `llvm-config` used for the build            |
| `XLANG_COLOR` / `NO_COLOR`                                      | Colour of diagnostics                           |

## Status

Early stage (v0.1). The `development` branch carries no backward compatibility: when behaviour
changes, the superseded form is removed from code, tests and docs with no note about the old form.
Open gaps are recorded in **[xlang-docs/known-limitations.md](xlang-docs/known-limitations.md)**.

## License

To be updated according to the project owner's license preference.
