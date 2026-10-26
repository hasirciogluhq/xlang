# xlang Linking Reference

This document describes how the compiler turns a program into an executable, shared library or static archive today. xlang uses no libc and no C runtime: the compiler links only its own output.

---

## Pipeline

```text
.xl sources -> parse/check -> codegen (LLVM) -> object (.o / .obj) -> link -> executable / shared library
                                                              \-> in-process archiver -> static library
```

`xlang build file.xl` runs the whole pipeline. `--emit` selects where it stops or what it produces: `exe` (default), `obj`, `ir`, `bc`, `header`, `static` (archive), `shared`. `xlang run file.xl` builds into a temporary executable and runs it.

---

## What gets linked

- The program's object code, including the stdlib modules it imports (the stdlib is embedded in the compiler; `--no-std` and `--std=<dir>` change that).
- For an executable, the compiler-generated entry stub (see below).
- Nothing else: there is no C runtime, no implicit `-lc`, no `-pthread`. Memory routines such as `memcpy` are stdlib code exported with C names.
- `--incremental` writes one object per module into a cache directory (`--cache-dir`, default `.xlang-cache` next to the output) and reuses unchanged ones.

There is no way to pass extra objects or archives on the command line.

---

## Linkers per OS

The compiler runs an external linker found on `PATH`; it is not bundled:

| Target OS | Tool | Notes |
|-----------|------|-------|
| Linux | `ld.lld` or `ld` | `-nostdlib -static`, entry `_start`, `--gc-sections`; `-shared` for shared libraries |
| macOS | `ld64.lld` | entry `_start`, `-dead_strip`, `-dylib` for shared libraries |
| Windows | `lld-link` | `/nodefaultlib`, entry `mainCRTStartup`, `/subsystem:console`, `/dll` for shared libraries |

If the tool is missing, the build fails with a message naming it; `--emit=obj` needs no linker. Cross-compilation uses `--target <triple>` and needs a linker for that target.

---

## Entry stub

For an executable, the compiler emits a small assembly stub as the OS entry symbol when the program defines `main`. The stub calls the compiler-generated `_xl_initAll` (which runs the `@init` functions in order) and then `main`, and exits with `main`'s integer result (0 for a void `main`). Stubs exist for Linux and macOS on x86_64 and aarch64 and for Windows on x86_64; the Windows stub exits through ntdll's `NtTerminateProcess` and also defines `__chkstk` and `_fltused`. Without a `main` no stub is emitted.

---

## External symbols and native libraries

```
declare fn helper(x: i32) -> i32
@library("ntdll") declare fn NtTerminateProcess(handle: i64, status: i32) -> i32
```

- A `declare`d function that no module of the compilation defines produces warning `W0301`; the final link then fails with an undefined symbol unless something else supplies it.
- `@library("name")` is used only on Windows: the compiler generates an import library for each named DLL from the declared symbols (with `llvm-dlltool`, so no Windows SDK is needed) and passes it to `lld-link`. On Linux and macOS no library is ever linked.
- OS libraries are used only where raw system calls are not an option (Windows `ntdll`, `ws2_32`, `bcrypt`, `crypt32`). Their declarations stay in `std/sys/windows.xl`.

---

## Static and shared output

- `--emit=static` archives the program's objects in-process (no `ar`).
- `--emit=shared` links a shared library (`-shared`, `-dylib` or `/dll`); no entry stub is added.
- `--emit=header` writes a C header for `export`ed items.
- `export(c)` keeps a symbol name unmangled so C code can link to it; aggregates are passed through pointers, not the target C ABI (see `KNOWN-LIMITATIONS.md`).

---

## Related documents

| Document | Role |
|----------|------|
| [Language Reference](LANGUAGE.md) | `declare`, `@library`, `export(c)`, entry points |
| [Package Manager](PACKAGE_MANAGER.md) | Planned package tooling (not implemented) |
