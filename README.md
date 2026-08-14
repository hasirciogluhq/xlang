# xlang

**xlang** is an extremely low-level, LLVM-based programming language. Optional high-level APIs (`net`/`http`, and similar) are importable packages — not the language core. The `xlang` CLI compiles `.xlang` to native objects / executables / static libraries. Frontend runtime is written in xlang; C++ owns the compiler, host tooling, and LLVM codegen. OS userspace helpers live in C bridges under `xl_*`.

```
┌─────────────────┐     ┌──────────────────────────┐     ┌────────────────┐
│  .xlang source  │ ──► │  xlang (C++ / LLVM API)   │ ──► │  .o / exe / .a │
└─────────────────┘     └──────────────────────────┘     └────────────────┘
                               │
                CommonIrBuilder → Module → TargetMachine
                               │
          runtime frontend (.xlang) + OS bridges (xl_* C)
```

## Features

| Area | Support |
|------|---------|
| Types | `int32`, `int64`, `float`, `double`, `bool`, `string`, struct, pointer, array |
| Functions | Overload, variadic (`...`), `export` / `external` / `declare` |
| Modules | `import`, `import * as`, directory packages (`http/`) |
| Memory | `new` / `delete`, struct fields, heap |
| Control flow | `if` / `else`, `while` |
| Strings | Concat (`+`), `printf`-style formatted `print` |
| Concurrency | `spawn` / `wait_all`, `sync` / `scheduler` |
| Native kernel | `declare syscall <n> name(...)` or `@syscall(n, args...)` |
| Bridges | plain `declare xl_<object>_<action>(...)` → link OS bridge `.a` |

Full language reference: **[docs/LANGUAGE.md](docs/LANGUAGE.md)**  
Runtime / embed: **[docs/RUNTIME.md](docs/RUNTIME.md)** · Bridge ABI: **[docs/BRIDGE_ABI.md](docs/BRIDGE_ABI.md)** · Linking: **[docs/LINKING.md](docs/LINKING.md)**  
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

xmake builds **C/C++ only** (compiler + host OS bridge static libs). Frontend `.xlang` under `src/runtime/frontend` is compiled by `xlang` itself (dev: in-tree; release: two-phase embed — see [RUNTIME.md](docs/RUNTIME.md)).

Release embed dance:

```bash
xmake                          # stage-1 xlang
xmake build_bridges_pack
xmake bootstrap_embed          # compile frontend runtime + emit build/embed/*.c
xmake -r xlang                 # stage-2 with XLANG_HAS_EMBEDDED_*
```

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

Defaults for `xlang build foo.xlang`: **executable** + runtime **on** + bridges **on** + OS baseline syslibs (e.g. `-pthread`).

### `xlang build`

```bash
xlang build app.xlang                              # executable (default)
xlang build app.xlang lib.o -o myapp
xlang build app.xlang --build=object -o app.o
xlang build app.xlang --build=static -o libapp.a
xlang build app.xlang --build=shared               # experimental
xlang build app.xlang --no-runtime                 # no frontend runtime; bridges still link
xlang build app.xlang --no-bridge                  # raw; user supplies symbols
xlang build app.xlang --runtime path/to/runtime.xlang
xlang build app.xlang --bridge path/to/bridges.a
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
xlang run program.xlang --runtime path/to/runtime.xlang
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
declare xl_net_tcp_connect(host: string, port: int32): int64   // bridge / C ABI
declare syscall 1 write(fd: int64, buf: int64, n: int64): int64 // CPU-native trap
// or: @syscall(1, fd, buf, n)
```

See [BRIDGE_ABI.md](docs/BRIDGE_ABI.md).

## Tree

```
xlang/
├── src/
│   ├── cli/                 # main
│   ├── lang/                # ast, lexer, parser, types
│   ├── codegen/             # CommonIrBuilder, TargetMachine, lowering
│   ├── compiler/            # compile / link / module / runtime / test
│   ├── host/                # layout, resolve, fetch, embed
│   ├── platform/            # linux | macosx | windows
│   ├── util/
│   └── runtime/
│       ├── RUNTIME_VERSION
│       ├── SUPPORTED_COMPILERS
│       ├── bridge/{linux,macosx,windows}/   # xl_* C ABI
│       └── frontend/                        # .xlang packages
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
4. **Link** — user objects + runtime (unless `--no-runtime`) + bridges (unless `--no-bridge`) + OS baseline syslibs  

Bridge ≠ kernel: bridges are userspace `xl_*` helpers. Kernel entry is only via `declare syscall` / `@syscall`.

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
