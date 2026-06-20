# xlang

**xlang** is an LLVM-based programming language targeting self-hosting. The `xlank` compiler produces native executables or object files from `.xlang` source. The runtime (print, scheduler, spawn) is largely **written in xlang**; C++ only provides the compiler, OS bridge (syscall), and LLVM codegen layer.

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────┐
│  .xlang source  │ ──► │  xlank (C++/LLVM) │ ──► │  executable │
└─────────────────┘     └──────────────────┘     └─────────────┘
                               │
                               ▼
                        runtime/*.xlang
                        (print, scheduler, spawn)
                               │
                               ▼
                        syscall bridge
                        (pthread, sysconf, …)
```

## Features

| Area | Support |
|------|---------|
| Types | `int32`, `int64`, `float`, `double`, `bool`, `string`, struct, pointer, array |
| Functions | Overload, variadic (`...`), `export` / `external` |
| Modules | `import`, `from … import`, multi-file compilation |
| Memory | `new` / `delete`, struct fields, heap |
| Control flow | `if` / `else`, `while` |
| Strings | Concat (`+`), `printf`-style formatted `print` |
| Concurrency | Go-routine-like `spawn` / `wait_all` (scheduler in xlang) |
| Linking | Link external `.o` files |

Full language reference: **[docs/LANGUAGE.md](docs/LANGUAGE.md)**

## Requirements

- **CMake** ≥ 3.16
- **C++17** compiler (Clang or GCC)
- **Clang** (for LLVM IR → object → executable linking; `clang` must be on PATH)

## Build

```bash
mkdir -p build && cd build
cmake ..
make -j4
```

Compiler binary: `./build/xlank`

Runtime sources live in `runtime/` and are embedded into the binary at build time. During development, set `XLANG_RUNTIME_DIR` to use the runtime from the source tree.

## Quick start

```bash
# Hello world
./build/xlank run examples/strings.xlang

# Module import
./build/xlank run examples/hello.xlang

# Struct + heap
./build/xlank run examples/types.xlang

# Parallel scheduler
./build/xlank run examples/scheduler.xlang
```

## CLI

### `xlank run`

Compiles source, links with runtime, and runs the program.

```bash
xlank run program.xlang
xlank run main.xlang lib.o          # link additional object files
xlank run program.xlang --keep-artifacts
xlank run program.xlang --runtime path/to/runtime.xlang
```

### `xlank build`

Produces an executable or object file.

```bash
xlank build app.xlang                        # → app (executable)
xlank build app.xlang lib.o -o myapp         # link with lib.o
xlank build lib.xlang --build=lib -o lib.o   # object library
xlank build app.xlang --emit-ir              # write LLVM IR
xlank build app.xlang --skip-runtime         # no runtime (no print/spawn)
xlank build app.xlang --keep-ir
xlank build app.xlang -o output/path
```

### `xlank parse`

Shows an AST summary (debug).

```bash
xlank parse examples/hello.xlang
```

## Project layout

```
xlang/
├── src/              # xlank compiler (lexer, parser, codegen, linker)
├── include/xlang/    # C++ headers
├── runtime/          # Self-hosting runtime (xlang)
│   ├── runtime.xlang     # print export
│   └── scheduler.xlang   # spawn, wait_all, worker pool
├── examples/         # Sample programs
├── test/             # Module + link tests
├── cmake/            # Runtime embed script
└── docs/
    └── LANGUAGE.md   # Language reference
```

## Architecture overview

### Compiler (`xlank`)

1. **Lexer / Parser** — source → AST  
2. **Module loader** — merges files via `import`  
3. **Codegen** — AST → LLVM IR  
4. **Syscall lowering** — `declare syscall` → OS/pthread bridge (C++ IR)  
5. **Clang** — IR → `.o` → executable (+ runtime `.o`)

### Runtime

User programs are linked with the **runtime** by default. The runtime provides:

- **`print(...)`** — variadic, single function (`printf`-based)
- **`spawn(bound_call)`** — Go-routine-like task queue
- **`wait_all()`**, **`cpu()`**, **`add_worker()`**

Scheduler logic (`queue`, `worker_loop`, mutex/cond) lives entirely in `runtime/scheduler.xlang`. Syscalls are only for OS access such as CPU count, mutex, condition variables, and thread creation.

### Library + external linking

```bash
# lib.xlang → lib.o
xlank build test/lib.xlang --build=lib -o test/lib.o

# main.xlang declares external function, lib.o is linked
xlank run test/main.xlang test/lib.o
```

`test/main.xlang`:

```xlang
declare external fn zamazokka(str)

fn main() {
    zamazokka(1978)
}
```

## Environment variables

| Variable | Description |
|----------|-------------|
| `XLANG_PATH` | Colon-separated module search directories (`modul.xlang`) |

## Status

xlang is an early-stage language (v0.1). APIs and syntax may change. See the **Limitations** section in [docs/LANGUAGE.md](docs/LANGUAGE.md) for known constraints.

## License

To be updated according to the project owner's license preference.
