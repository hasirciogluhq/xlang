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
- Coded diagnostics with fixes (`xlc --explain E0709`).

## Repositories

| Repository | Content |
| --- | --- |
| [xlang](https://github.com/hasirciogluhq/xlang) | the `xl` tool, written in xlang (this repository) |
| [xlc](https://github.com/hasirciogluhq/xlc) | the compiler (submodule `xlc/`) |
| [xl-std](https://github.com/hasirciogluhq/xl-std) | the standard library and its tests (submodule `xl-std/`) |
| [xlang-docs](https://github.com/hasirciogluhq/xlang-docs) | documentation site (submodule `xlang-docs/`) |
| [xl-vscode](https://github.com/hasirciogluhq/xl-vscode) | VS Code extension (submodule `xl-vscode/`) |

Clone with `git clone --recurse-submodules https://github.com/hasirciogluhq/xlang.git`.

## Install

Releases exist for Linux and macOS (x86-64, arm64) and Windows (x86-64). The installer puts the newest `xl` into `~/.local/bin`, verifies every archive against the release's `checksums.txt`, and then runs `xl lc install`, which installs the compiler and the stdlib under `~/.xlang`.

```bash
curl -fsSL https://github.com/hasirciogluhq/xlang/releases/download/v1.0.0-rc.2/install.sh | bash
```

```powershell
irm https://github.com/hasirciogluhq/xlang/releases/download/v1.0.0-rc.2/install.ps1 | iex   # Windows
```

`bash -s -- --version latest-rc` (or `latest-stable`, or a tag) picks another release.

The compiler links with LLVM's `lld`, which must be on `PATH`. Details: [installation](xlang-docs/src/content/docs/compiler/install.md).

## Usage

```bash
xl run hello.xl a b                   # build to a temporary file and run with arguments
xl build app.xl -O2 -o app            # executable; --emit=obj|ir|bc|header|static|shared
xl build app.xl --target aarch64-apple-darwin
xl check app.xl                       # type-check only
xl env app.xl                         # the compiler, stdlib and package roots a build uses
xl pkg install github.com/user/repo   # add a package; also add, update, remove, list, clean
xl lc update                          # the newest compiler and stdlib
xl self-update                        # the newest xl
```

The commands are documented in [the xl tool](xlang-docs/src/content/docs/compiler/cli.md); the language and the standard library in [xlang-docs](https://hasirciogluhq.github.io/xlang-docs/).

## Build from source

`xl` is written in xlang (`src/`, layout in [src/README.md](src/README.md)); the compiler is [`xlc`](https://github.com/hasirciogluhq/xlc), built in its own repository. With a compiler on `PATH`:

```bash
cd src
xlc main.xl -M std=../xl-std --import std -DfeatureDefaultAllocator=true '-DxlVersion="1.0.0-dev"' -O2 -o ../xl
cd .. && test/run.sh ./xl "$(command -v xlc)" xl-std
```

## Status

`xl` is at `1.0.0-rc.1`, a release candidate; the language and the compiler are pre-1.0. The `development` branch carries no backward compatibility: when behavior changes, the old form is removed from code, tests and docs. Open gaps are recorded in the [known limitations](xlang-docs/src/content/docs/known-limitations.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Report vulnerabilities as described in [SECURITY.md](SECURITY.md). This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

Licensed under the [Apache License, Version 2.0](LICENSE).
