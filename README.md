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
| Modules | `import`, `import * as`, directory packages (`http/`) |
| Memory | `new` / `delete`, struct fields, heap |
| Control flow | `if` / `else`, `while` |
| Strings | Concat (`+`), `printf`-style formatted `print` |
| Concurrency | user-defined functions and linked objects |
| Native kernel | `declare syscall <n> name(...)` or `@syscall(n, args...)` |
| External symbols | plain `declare name(...)` → resolve from user-provided objects |

Full language reference: **[docs/LANGUAGE.md](docs/LANGUAGE.md)** · Linking: **[docs/LINKING.md](docs/LINKING.md)**  
VS Code extension: **[vscode/README.md](vscode/README.md)**

## Requirements

- [xmake](https://xmake.io) ≥ 2.8
- **C++23** compiler (Clang or GCC)
- **Clang** on `PATH` (used when linking the final image)
- LLVM (via xmake package)

## Build

```bash
xmake
# optional: xmake f -m debug && xmake
```

Compiler binary: `./build/xlang`

xmake builds the C/C++ compiler and host tooling only.

## Quick start

```bash
./build/xlang run examples/strings.xlang
./build/xlang run examples/hello.xlang
./build/xlang run examples/types.xlang
./build/xlang run examples/scheduler.xlang
./build/xlang run examples/pointers.xlang
./build/xlang run examples/filesystem.xlang
./build/xlang run examples/http_server.xlang
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
