# xlang

**xlang** is an LLVM-based programming language targeting self-hosting. The compiler binary (`xlang`) produces native executables or object files from `.xlang` source. The runtime (print, scheduler, spawn) is largely **written in xlang**; C++ only provides the compiler, OS bridge (syscall), and LLVM codegen layer.

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────┐
│  .xlang source  │ ──► │  xlang (C++/LLVM) │ ──► │  executable │
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
| Modules | `import`, `import * as`, directory packages (`libs/http/`), `XLANG_PATH` |
| Memory | `new` / `delete`, struct fields, heap |
| Control flow | `if` / `else`, `while` |
| Strings | Concat (`+`), `printf`-style formatted `print` |
| Concurrency | `spawn` / `wait_all`, `sync` module (Lock, RWLock, AtomicInt) |
| Linking | Link external `.o` files |

Full language reference: **[docs/LANGUAGE.md](docs/LANGUAGE.md)**  
VS Code extension (IntelliSense, hover, diagnostics): **[vscode/README.md](vscode/README.md)**

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

Compiler binary: `./build/xlang`

Runtime sources live in `runtime/` and are embedded into the binary at build time. During development, set `XLANG_RUNTIME_DIR` to use the runtime from the source tree.

## Quick start

```bash
# Hello world
./build/xlang run examples/strings.xlang

# Module import
./build/xlang run examples/hello.xlang

# Struct + heap
./build/xlang run examples/types.xlang

# HTTP server
./build/xlang run examples/http_server.xlang
```

## VS Code extension

IntelliSense, hover documentation, import-aware completions, diagnostics, and run/test commands:

```bash
cd vscode && bun install && bun run compile && bun run package
code --install-extension xlang-1.0.0.vsix
```

See [vscode/README.md](vscode/README.md) for details.

## CLI

### `xlang run`

Compiles source, links with runtime, and runs the program.

```bash
xlang run program.xlang
xlang run main.xlang lib.o          # link additional object files
xlang run program.xlang --keep-artifacts
xlang run program.xlang --runtime path/to/runtime.xlang
```

### `xlang build`

Produces an executable or object file.

```bash
xlang build app.xlang                        # → app (executable)
xlang build app.xlang lib.o -o myapp         # link with lib.o
xlang build lib.xlang --build=lib -o lib.o   # object library
xlang build app.xlang --emit-ir              # write LLVM IR
xlang build app.xlang --skip-runtime         # no runtime (no print/spawn)
xlang build app.xlang --keep-ir
xlang build app.xlang -o output/path
```

### `xlang parse`

Shows an AST summary (debug).

```bash
xlang parse examples/hello.xlang
```

### `xlang test`

Runs Vitest-style tests from `*.test.xlang` files (one process per file).

```bash
xlang test                    # default: test/xlang/
xlang test http               # pattern filter
xlang test --parallel         # parallel Test* functions
```

Test API (`libs/test.xlang`): `expect(actual).toEqual(expected)`, `expectFn(fn).toThrow()`, `Test*` functions.

```xlang
import test from test
import router from http/router

fn TestPingRoute() {
    local r = router.NewRouter()
    r.Get("/ping", handle_ping)
    local ctx = r.DispatchRequest("GET", "/ping")
    expect(ctx.status).toEqual(200)
    return 0
}

fn handle_ping(ctx: Context) {
    ctx.String(200, "pong")
    expect(1).toEqual(1)
    return 0
}
```

---

```
xlang/
├── src/              # xlang compiler (lexer, parser, codegen, linker)
├── include/xlang/    # C++ headers
├── runtime/          # Embedded runtime package (print, scheduler, net, errors)
├── libs/             # Importable libraries (json, http/, test, process)
│   └── http/         # Router + TCP server (router.xlang)
├── examples/         # Sample programs
├── test/xlang/       # *.test.xlang suite
├── vscode/           # VS Code extension (IntelliSense, hover, diagnostics)
├── cmake/            # Embed scripts (runtime + libs)
└── docs/LANGUAGE.md
```

## Architecture overview

### Compiler (`xlang`)

1. **Lexer / Parser** — source → AST  
2. **Module loader** — merges files via `import`  
3. **Codegen** — AST → LLVM IR  
4. **Syscall lowering** — `declare syscall` → OS/pthread bridge (C++ IR)  
5. **Clang** — IR → `.o` → executable (+ runtime `.o`)

### Runtime

User programs are linked with the **runtime** by default (`runtime/` package):

- **`print(...)`** — variadic formatted output
- **`fetch(url)`** — HTTP/HTTPS GET (`runtime/net.xlang`)
- **`spawn` / `wait_all` / `cpu` / `add_worker`** — scheduler (`runtime/scheduler.xlang`)
- **`sync`** — `Lock`, `RWLock`, `AtomicInt` with method API (`l.Lock()`, `a.FetchAdd()`)

Importable **libs** (embedded at compile time):

- **`json`** — parse + typed field access
- **`http`** — Gin-style router (`Context`, `r.Use`, `ctx.JSON`, `r.ListenAndServe`)
- **`test`** — Vitest-style `expect`
- **`process`** — fork, pipe, fd, env, `file_read`

### Library + external linking

```bash
# lib.xlang → lib.o
xlang build test/lib.xlang --build=lib -o test/lib.o

# main.xlang declares external function, lib.o is linked
xlang run test/main.xlang test/lib.o
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
| `XLANG_PATH` | Colon-separated module search directories |
| `XLANG_RUNTIME_DIR` | Override runtime source tree (development) |
| `XLANG_LIBS_DIR` | Override libs source tree (development) |

## Status

xlang is an early-stage language (v0.1). APIs and syntax may change. See the **Limitations** section in [docs/LANGUAGE.md](docs/LANGUAGE.md) for known constraints.

## License

To be updated according to the project owner's license preference.
