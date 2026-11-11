# xlang

A low-level, LLVM-based systems programming language with no libc and no C runtime. The compiler knows the language, the CPU primitives and a closed list of builtins; memory, I/O, threads, sockets, HTTP and TLS are ordinary xlang code in the standard library.

[Documentation](https://hasirciogluhq.github.io/xlang-docs/) · [Known limitations](xlang-docs/src/content/docs/known-limitations.md) · [Contributing](CONTRIBUTING.md) · [License](LICENSE)

```xlang
from "std" import print

@entry fn main() -> i32 {
    for i in 1..=3 { print(i) print("\n") }
    return 0
}
```

## Highlights

- Native objects, executables, static and shared libraries, LLVM IR and C headers for Linux, macOS and Windows (x86-64, arm64).
- Structs, classes with single inheritance, interfaces, payload enums, monomorphized generics, tuples, slices, `defer`, `try`/`catch`, hygienic macros.
- Bounds-checked arrays by default; allocator, panic and unwind are replaceable stdlib functions.
- Packages are plain git repositories of `.xl` source, pinned by `xlang.lock`; builds never use the network.
- Coded diagnostics with fixes (`xl --explain E0709`).

## Repositories

| Repository | Content |
| --- | --- |
| [xlang](https://github.com/hasirciogluhq/xlang) | `xlang` CLI, build, integration (this repository) |
| [xlc](https://github.com/hasirciogluhq/xlc) | the compiler (submodule `xlc/`) |
| [xl-std](https://github.com/hasirciogluhq/xl-std) | the standard library and its tests (submodule `xl-std/`) |
| [xlang-docs](https://github.com/hasirciogluhq/xlang-docs) | documentation site (submodule `xlang-docs/`) |
| [xl-vscode](https://github.com/hasirciogluhq/xl-vscode) | VS Code extension (submodule `xl-vscode/`) |

Clone with `git clone --recurse-submodules https://github.com/hasirciogluhq/xlang.git`.

## Install

Prebuilt binaries exist for macOS and Linux (arm64, amd64). The installer verifies the SHA-256 of the archive and installs the latest release into `~/.local/bin` (`XLANG_INSTALL_DIR`, `XLANG_VERSION` override).

```bash
curl -fsSL https://raw.githubusercontent.com/hasirciogluhq/xlang/main/scripts/install.sh | bash
brew install hasirciogluhq/tap/xlang      # alternative
xl self-update                         # later updates
```

On Windows, use [WSL](https://learn.microsoft.com/windows/wsl/install); the compiler still produces Windows executables (`--target x86_64-pc-windows-msvc`). Shell completion for bash, zsh and fish is set up by every install method. Details: [installation](xlang-docs/src/content/docs/compiler/install.md).

## Usage

```bash
xl run hello.xl                       # build to a temporary file and run
xl build app.xl -o app                # executable; --emit=obj|ir|bc|header|static|shared
xl build app.xl --target aarch64-apple-darwin -O2
xl check app.xl                       # type-check only
xl test xl-std/test/unit/             # run *.test.xl suites
xl install github.com/user/repo
xl --explain E0709                    # explain a diagnostic code
```

The full CLI, language reference and standard library are documented in [xlang-docs](https://hasirciogluhq.github.io/xlang-docs/).

## Build from source

The compiler is [`xlc`](https://github.com/hasirciogluhq/xlc) (built there); `xl` is the all-in-one tool. This repository has no build system: the C++ under `src/` is being rewritten in xlang.

The repository layout is described in [src/README.md](src/README.md).

## Status

Pre-1.0. The `development` branch carries no backward compatibility: when behavior changes, the old form is removed from code, tests and docs. Open gaps are recorded in the [known limitations](xlang-docs/src/content/docs/known-limitations.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Report vulnerabilities as described in [SECURITY.md](SECURITY.md). This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

Licensed under the [Apache License, Version 2.0](LICENSE).
