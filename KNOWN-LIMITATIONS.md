# Known limitations and open decisions

Everything in the original plan is implemented; this file lists what is intentionally incomplete.

## Platform support
- **macOS raw threads:** `bsdthread_register` returns `EINVAL` in a process that did not come up through
  libSystem, so `bsdthread_create` could not be tried. The scheduler runs cooperatively on one thread
  there; kqueue I/O works. Options: find the registration prerequisite, use Mach `thread_create_running`
  (MIG message), or keep the single-thread mode.
- **Windows:** stdlib (`std/sys/windows.xl`) and the entry stub are written but unverified. NT syscall
  numbers (SSN) come from `-D ssnNtWriteFile=...` and friends; the entry (`mainCRTStartup`) calls `main`
  and traps because the exit SSN is not known to the compiler. `syscall` with more than 4 arguments
  (stack passing) is a compile error. No threads or readiness polling.

## ABI and interop
- **`export(c)` and aggregates by value:** structs are passed/returned through pointers (the xl convention),
  not through the target C ABI (SysV/AAPCS register passing). Only pointers, scalars and `@repr(c)`
  structs behind pointers are safe across the boundary.
- **Itanium typeinfo:** the vtable of an `@repr(c)` class has a `null` typeinfo slot; no `_ZTI...` symbol.

## Language/stdlib
- No bit operators, compound assignment, `break`/`continue` or `for`; the stdlib is written without them.
- `HIR` does not fold constant expressions (`N * 2` is constant in global initializers but folded later).
- No flow analysis for falling off the end of a value-returning function.
- Module resolution: `std`/`std/x` come from the stdlib root; other paths from the importing file's
  directory, then the root file's directory. `import "x"` without alias binds the last path segment.
  No package or search-path strategies.
- The default stdlib is embedded in the compiler (generated `build/gen/std_blob.cpp`, rebuilt when
  `std/**/*.xl` changes); `--std=<dir>` replaces it. The compiler never searches the file system for it.

## Stdlib: files, JSON, HTTP
- **`std/http` is plain HTTP only:** no TLS and no DNS (the client takes an IPv4 address), so `https://` URLs
  cannot be fetched. The server handles one connection at a time (blocking `accept`, `Connection: close`),
  needs the request line and headers within 8 KiB, and does not read request bodies beyond what arrived
  with the headers. Routes match the exact path; there are no route groups or path parameters.
- **`std/json` is read-only and flat:** lookups of top-level members of an object in a NUL-terminated buffer;
  no nested paths, no `\u` escapes, no validation, no writer.
- **`std/fs` has no directories or metadata** beyond existence and size; the caller supplies all buffers.
- **Windows:** the new file and TCP calls in `std/sys/windows.xl` are stubs returning -1 (no NT SSNs).

## Tooling
- Debug info has compile unit, subprograms, line locations and basic types; no struct/class types.
- `--incremental` caches per-module objects only; semantic analysis still runs for every module (no
  interface files). A MIR layer (plan 8.3) was not needed.
