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

- The program's object code, including the stdlib modules it imports (the stdlib is embedded in the compiler; `--no-std`, `--std=<dir>` and `--std=host/user/repo` change that) and the modules of the installed packages it imports.
- Prebuilt object files and archives that installed packages list in `[[native]]` entries matching the target (executables and shared libraries only; see [Packages]({{ site.baseurl }}/compiler/packages.html)).
- For an executable, the compiler-generated entry stub (see below).
- Nothing else: there is no C runtime, no implicit `-lc`, no `-pthread`. Memory routines such as `memcpy` are stdlib code exported with C names.
- `--incremental` writes one object per module into a cache directory (`--cache-dir`, default `.xlang-cache` next to the output) and reuses unchanged ones.

There is no way to pass extra objects or archives on the command line; a package supplies them through `[[native]]`. A static library built by xlang contains the stdlib modules it imported, so a library meant to be linked into another xlang program is built with `--no-std`.

---

## Linkers per OS

The compiler runs an external linker found on `PATH`; it is not bundled:

| Target OS | Tool | Notes |
|-----------|------|-------|
| Linux | `ld.lld` or `ld` | `-nostdlib -static`, entry `_start`, `--gc-sections`, `-l<name>` for used `@library` names; `-shared` for shared libraries |
| macOS | `ld64.lld` | entry `_start`, `-dead_strip`, `-syslibroot <SDK>` and `-l<name>` for used `@library` names, `-dylib` for shared libraries |
| Windows | `lld-link` | `/nodefaultlib`, entry `mainCRTStartup`, `/subsystem:console`, `/dll` for shared libraries |

If the tool is missing, the build fails with a message naming it; `--emit=obj` needs no linker. Cross-compilation uses `--target <triple>` and needs a linker for that target.

---

## Entry stub

For an executable, the compiler emits a small assembly stub as the OS entry symbol. The stub calls the compiler-generated `_xl_initAll` (which runs the `@init` functions in order), then the `@entry` function, then `_xl_finiAll` (which runs the `@fini` functions in reverse order), and exits with the `@entry` function's integer result (0 when it returns nothing). Stubs exist for Linux and macOS on x86_64 and aarch64 and for Windows on x86_64; the Windows stub exits through ntdll's `NtTerminateProcess` and also defines `__chkstk` and `_fltused`. Without an `@entry` function the build fails with `E0739`, so no stub is emitted for static libraries or object files.

---

## External symbols and native libraries

```
declare fn helper(x: i32) -> i32
@library("ntdll") declare fn NtTerminateProcess(handle: i64, status: i32) -> i32
```

- A `declare`d function that no module of the compilation defines produces warning `W0301`; the final link then fails with an undefined symbol unless something else supplies it.
- `@library("name")` works on all three OSes, and only for declarations that the program reaches (see "Reachability pruning" below):
  - Windows: the compiler generates an import library for each named DLL from the declared symbols (with `llvm-dlltool`, so no Windows SDK is needed) and passes it to `lld-link`.
  - macOS: each used library name is passed as `-l<name>`, with `-syslibroot` set to the SDK from `xcrun --show-sdk-path`, so `@library("System")` links `libSystem`.
  - Linux: each used library name is passed as `-l<name>` after the objects. The link is static, and the compiler adds no search path, so the archive must be found by the linker (a package supplies it through `[[native]]`).
- OS libraries may be declared in any stdlib area (for example `std/thread` may create a thread through a library call); user code reaches them only through stdlib functions.

## Reachability pruning

Before code generation the compiler removes every function and global that no root reaches (`src/hir/prune.cpp`). The roots are `@entry`, every `@init` and `@fini` function, every `register(role)` function, the hooks codegen calls by name (`panic`, `alloc`, `free`, `unwind`), every vtable and interface method, the C functions LLVM may call implicitly (`memcpy`, `memmove`, `memset`, `memcmp`, `bcmp`, `bzero`), and, for libraries and objects, every `export` and `export(c)` symbol. Everything else is kept only if reachable from a root through calls, function values, constructors, globals and cleanup bodies. The `@library` imports of the removed functions are never linked. The stdlib panic path writes through `sysWriteFile`, which does not reach the Windows socket code, so a program that does not use sockets does not import `ws2_32`.

---

## Static and shared output

- `--emit=static` archives the program's objects in-process (no `ar`).
- `--emit=shared` links a shared library (`-shared`, `-dylib` or `/dll`); no entry stub is added.
- `--emit=header` writes a C header for `export`ed items.
- `export(c)` keeps a symbol name unmangled so C code can link to it; aggregates are passed through pointers, not the target C ABI (see [Known limitations]({{ site.baseurl }}/known-limitations.html)).

---

## Related documents

| Document | Role |
|----------|------|
| [Language Reference]({{ site.baseurl }}/language/index.html) | `declare`, `@library`, `export(c)`, entry points |
| [Packages]({{ site.baseurl }}/compiler/packages.html) | Dependencies from git repositories, `[[native]]` prebuilt files |
