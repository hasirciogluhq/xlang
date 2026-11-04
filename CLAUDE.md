# xlang — notes for Claude

## Platforms (hard rule)
- Write code for Linux, macOS and Windows (x86-64 and arm64 where the OS supports it). Every std feature is written for all three; where one OS cannot support a feature, it is a clear compile-time error or a documented gap in `docs/known-limitations.md`, not a half-written port.
- Windows e2e entries are verified under Wine/CrossOver or on real Windows; a Windows column of `0` means not yet verified, not out of scope.

## Native OS code (hard rule)
- Native OS code (syscalls, ntdll/kernel32/ws2_32 imports, module and process handles) lives in `std/sys/`. Do not create one file per OS by default: a file holds one area (socket, process, file, memory, entropy) and routes between OSes with `#if os`.
- Split an area per OS only when the bodies share nothing, and then keep the shared signature in one place.
- Use one function for all OSes when the arguments and result are the same; split into per-OS functions only when the argument or result differs, and route with `#if` at the smallest possible scope.
- **Syscalls are docs-first (hard rule).** Before writing or changing any syscall or OS API call, read the official documentation for every target OS (Linux man-pages and kernel headers, macOS/XNU syscall headers, Windows API and ntdll docs) and record the chosen numbers, struct layouts and error behavior in `docs/` first. Never guess a number, a flag or a layout.
- A Windows shared library gets a `DllMain`: `DLL_PROCESS_ATTACH` runs `_xl_initAll` and `DLL_PROCESS_DETACH` runs `_xl_finiAll`; the module handle is obtained through a `std/sys` helper, not hard-coded.
- **OS setup is hidden (hard rule).** Per-OS one-time state (WSAStartup on Windows, the process-wide SIGPIPE disposition on Linux, anything similar) never reaches the user API. Each area has one internal `…Up()`-style function that routes with `#if os` and is called by every public creator (e.g. `sockUp` in `std/sys/sock.xl`). A user program opens, listens and connects without naming any OS call.
- **Stdlib comments (hard rule).** Stdlib comments are English and dense: explain each non-obvious layout, flag, errno and OS difference, and cite the specification or official page for every protocol or ABI rule (RFC number as `https://www.rfc-editor.org/rfc/rfcNNNN`, man-page or vendor docs as a URL).

## Language
- Repo content is English (code, comments, docs, commits, diagnostics). User chat is Turkish.

## C++ standard (hard rule)
- The toolchain supports C++23 and C++26 at the same time. Use C++26 features everywhere the compiler supports them; otherwise C++23 is the mandatory minimum. Writing C++20-or-older style code is forbidden.
- **Touch it, modernize it.** Any change to a C++ file, even a one-line fix, requires tracing the touched code and everything related to it (callers, callees, sibling helpers, headers, tests) and rewriting all of it to the latest C++ level. A small edit is never a reason to leave old-style code next to it.
- Use the modern language and library fully and idiomatically: concepts and constraints, `std::expected`, `std::optional`, `std::variant` with `std::visit`, `std::span`, `std::string_view`, ranges and views, `std::format`/`std::print`, structured bindings, `constexpr`/`consteval`, deducing `this`, `std::flat_map`, `std::mdspan`, `std::generator`, `std::ranges::to`, `std::unreachable`, `[[nodiscard]]`, designated initializers, `enum class` with `std::to_underlying`, pack indexing and contracts where available.
- Apply strategies and design patterns deliberately and correctly (RAII, visitor, strategy, CRTP/deducing-this, policy types, type erasure, builder, factory). No ad-hoc code where a pattern fits.
- Do not repeat long type names or boilerplate. Use `using` aliases (preferred over `typedef`) and macros where they remove repetition; keep them central and reusable. Code must be reusable, DRY, easy to re-check, and short: write less, get the best result.
- No raw owning pointers, no C-style arrays or casts, no manual `new`/`delete`, no C headers (`<stdio.h>` style), no out-parameters when a return value works, no macros where `constexpr`/templates work.

## File size (hard rule)
- There is no fixed line limit. The limit is dynamic: judge each file by complexity (distinct responsibilities, coupling, nesting) and by AI efficiency (how many tokens a model must read to edit it safely, and how many lines of unrelated code come along with one change).
- Rule of thumb: a file is too big when one edit forces reading much unrelated code, when it holds more than one responsibility, or when it no longer fits comfortably in a single Read (roughly above 1000 lines for dense code such as parser, sema, codegen). Simple, flat, table-like files may be longer; tangled files must be split earlier. Aim for one clear responsibility per file.
- Never edit a file that is too big by this judgment. If a change touches one, first split it into cohesive parts (`src/<area>/<topic>_<part>.cpp` plus a shared internal header), build, then make the change in the right part.
- A split is a pure move: no behavior change, no renames beyond what the move needs. Update `src/README.md` and the build file (`xmake.lua`) in the same change.
- New code goes into the matching part file, or a new part when none fits. Never append to a large catch-all file.

## Docs (single source, hard rule)
- `docs/` is the only documentation. It is published as the GitHub Pages site (`docs/_config.yml`, `docs/_data/nav.yml` for the sidebar). People and AI tools read the same pages; there is no separate AI docs folder.
- `docs/language/`: the language. `docs/compiler/`: install, linking, packages, runtime. `docs/std/`: the standard library, system call numbers. `docs/known-limitations.md`: open decisions and intentional incompleteness.
- Human pages are professional Markdown and readable without heavy formatting. Pages about the compiler are plain English prose.
- A new page is added to `docs/_data/nav.yml` (sidebar) and linked from `docs/index.md` in the same change.
- `src/README.md`: layout. `std/`: stdlib. `test/e2e/`: end-to-end programs.
- **Every code change must update the affected docs in the same change** (`docs/`, `src/README.md`, doc comments). A code update is not done until the docs match it.

## Core rules
- No compatibility layers. Language and compiler can change freely.
- **No BC for the development branch (hard rule).** The `development` branch carries no backward compatibility: when behavior changes, the superseded form is removed completely from code, tests, examples and docs, with no note about the old form.
- Old `.xlang` files, `examples/` and `.cursor/` no longer exist and must not be recreated; the only examples are the programs in `test/e2e/`.
- Maintain only `std/` (`.xl`) and `test/e2e/`.
- Compiler knows only the language, CPU primitives, and a closed builtin list (`syscall`, `__builtin_` atomics/fence/asm, `__builtin_read`/`__builtin_write`/`__builtin_drop`/`__builtin_needsDrop`, `__builtin_unwindPads`/`__builtin_unwindPadCount`, `sizeOf`-style, `staticAssert`, `nil`, `MemoryOrder`).
- Roles via `register`: `panic`, `alloc`, `free`, `unwind` (all functions). Bodies of builtins come from the compiler; everything else from stdlib.
- **Compiler level vs runtime level (hard rule).** The compiler owns language semantics, types, layout, CPU primitives and compile-time facts (`sizeOf`, `alignOf`, drop glue). Everything that happens at run time (memory acquisition, allocation policy, panic text/abort, I/O, threads, string/format machinery) is stdlib. The compiler must not know a runtime type, interface, struct or method by name (no `Allocator`, no `"alloc"`/`"free"` method lookup). The only compiler↔runtime seam is a `register(role)` function whose signature is checked structurally; a new runtime need becomes a new role function or stdlib code, never a name inside the compiler. Before adding a builtin, a role or a name check, ask whether stdlib can do it; if the compiler does need a hook, record it in `docs/known-limitations.md`.
- Entry symbols (`_start`, `mainCRTStartup`) and `_xl_initAll` are compiler-owned; user symbols with those names are `E0726`.
- `memcpy` family, `malloc`/`free`/allocator are **not** builtins. They live in stdlib (`std/memory`, `std/mem`).
- Compiler loads default stdlib but gives no special treatment to names inside it.
- **OS runtime libraries may be linked** (libSystem on macOS, ucrt/ws2_32/ntdll on Windows, a static or dynamic library on Linux), via `@library("name") declare fn`, on all three OSes the same way.
- **User code never calls them directly.** Any stdlib area may declare and call OS runtime functions (`@library`, behind `#if os` where the OS differs); user programs reach them only through stdlib functions. Example: `std/thread` may create a thread through a library call. `std/sys` holds the shared OS wrappers, but it is not the only place such calls live.
- **Only reachable code is linked.** The whole stdlib is available; after compilation, functions and imports that no reachable code uses are dropped, and only the rest is linked.
- Prefer raw `syscall` where the kernel number is documented and stable for the target; use `@library` where the OS promises the interface through its library (for example macOS `kevent`).
- Do not invent features. Record decisions in `docs/known-limitations.md` or ask.
- File content changes use the Read/Edit/Write tools. Bash is for build/run/inspect, and for moving or deleting whole files (`mv`, `git mv`, `rm`) when a reorganization needs it.

## Feature assessment
When user adds a feature, give honest assessment (no praise):

| Aspect         | Content |
|----------------|---------|
| Impact         | Complexity (low/medium/high), phases, maintenance |
| Value          | huge / good / marginal / junk (with reason) |
| Comparison     | vs clang/gcc, Rust, Zig, Go, Swift, Odin, etc. |
| Risk           | Pitfalls, conflicts, future cost |
| Recommendation | Now / later / reduced version + short alternative |

Object to bad ideas but leave decision to user. Cite real precedents. Do not re-ask closed topics (e.g. libc).

## Language summary
- Functions: `fn name(a: i32) -> i32 { }` (no `->` = void).
- Control: `while`, `do {} while`, `for`, `for i in lo..hi`, labeled `break`/`continue`, `switch` (no fallthrough), `defer`, `try { } catch e { }` (`e: *ui8` is the panic message, optional; a panic inside `try` runs the `defer`s and drops of every frame in between, then continues in `catch`; needs the `unwind` role, `E0501` otherwise), ternary, compound assignment, `x++`/`x--`. Generics on fn, struct, class, interface, alias and class method (monomorphized, no constraints); no tuples yet.
- Naming: types `PascalCase`, functions/vars/fields `camelCase`. `__builtin_` prefix stays. External names keep original spelling.
- Types: `i8..i64`, `ui8..ui64`, `f32/f64`, `bool`, `void`, `*T`, `fn(A)->R`, `[T; N]` (fixed array). Bounds check under `--safety=checked` (default).
- `enum Name: T { ... }` (default `i32`). Access `Name.A`. Explicit `as` for conversions. `MemoryOrder` is builtin enum.
- Diagnostics: `E` error, `W` warning (e.g. `E0501`, `W0402`).
- Implicit conversions: same-signedness integer widening only. Signed int → float warns (`W0601`). Unsigned ↔ signed is error (`E0601`). `unsafe` allows some relaxations; narrowing always needs `as`.
- `register(role) decl`: makes name global + exported. Roles: `panic` (`fn(*ui8)`, noreturn), `alloc` (`fn(usize, usize) -> *void`), `free` (`fn(*void)`), `unwind` (`fn(i32, *usize, *ui8)`, what `try` and landing pads call). One per role per group. Shadowing warns. Group 1 (stdlib) is the default; a group 2 (user) registration of the same role replaces it; a same-group duplicate is `E0502`; `register(start)` is `E0503`.
- `@init fn name() { }`: module-level boot function, no params, no return. Run order: stdlib group first, then user; per group, imports before importers, source order within a module. Calling it, or using its name as a value, is `E0727`; bad signature or method use is `E0729`.
- `__builtin_read(p)` / `__builtin_write(p, v)`: raw move through a pointer, no drop; `__builtin_write` is a statement (`E0709` as a value).
- Memory: default xlang layout; `@repr(c)` for C layout. `class` (single inheritance + vptr), `interface` (fat pointer).
- Conditional: `#if/#elif/#else/#endif` (normal expression syntax). Inactive regions only lexed.
- `type Name = T` (transparent). Macros: `define`, `define f(x: expr) { ... }`, call with `f!(...)`. Hygienic, no stringify.
- Source: `.xl`. `export` (modules) and `export(c)` (unmangled). Mangling on by default.
- `new T`: memory from the registered `alloc` fn; `new(p) T`: placement into pointer `p`, no allocation. `delete p`: drop, then the registered `free` fn (no explicit-allocator form). `E0501` if the needed role is unregistered. Stdlib registers defaults forwarding to `std/mem` `currentAllocator` (`default-allocator` feature).
- Globals: compile-time constant initializers only. Zero-init if none.
- Safety: `*T` unchecked, arrays checked by default. No borrow checker.
- Emit: `--emit=obj|ir|bc|header|exe|static|shared`. Compiler runs full pipeline by default. Links only its own output.
- Atomics: `__builtin_atomic*` + `__builtin_fence`. Order is compile-time `MemoryOrder`. Inline asm: `__builtin_asm(...)`.
- Global namespace: closed builtins + `register`d names. `print`/`alloc`/`memcpy`/`assert` come from stdlib via import.
- `declare`: single prefix for external things. Same attributes as normal decls.
- Stdlib optional (`--no-std`, `--std=<dir|host/user/repo[@tag]>`, `[std]` in `xlang.toml`). Features become `featureXxx` constants for `#if`.
- Packages: git repositories of `.xl` source (`docs/compiler/packages.md`). `xlang.toml` + `xlang.lock` + `.xlang/cache`; `xlang package install|update|remove|list|clean`; builds never use the network. Release tags `v?X.Y.Z`; `latest` = highest stable tag only (no fallback); `-suffix`/`-test` tags install only when named (`repo@v1.2.0-test`). No registry, no required layout, no solver. The compiler knows only "import path under a dependency path -> directory" and `[[native]]` link inputs.
- Two exports/imports of same name = error. Same entity via two paths = warning `W0401`.
- Layout: `@repr(c)` not required on `declare`. Versioned layout records.
- Diagnostics: warn with code + how to fix. `--allow`/`--deny`/`@allow`/`--explain`. Context lines configurable.
- Modules: `"x"` → `x.xl` or `x/index.xl`. Re-exports supported. `import "std"` gives everything.
- Import syntax: both `from "m" import x` and `import x from "m"` valid. Also `import *`.
- `syscall(nr, args...)`: dynamic arg count (target max checked at compile time). Args integer or `*T` (auto-converted). Returns `i64` (`-errno` style).

## Build
- xmake. `xmake build`. LLVM from `llvm-config` (static link).
- CLI: `xlang build|parse|check|layout|run|test|package|self-update|completions`, plus `--explain`, `--version`, `--color`, flags for defines/target/opt/debug/cpu/mangle/safety/std.
- Stdlib embedded at build time. `xmake build std` type-checks it.
- Tests: `xmake build xlang-tests && xmake run xlang-tests` (frontend/sema/HIR) and `xmake run e2e` (native + Docker + CrossOver for Windows).
- **Test order (hard rule).** The full suites take over a minute. While working, run only the tests of what changed, one by one (`xlang run test/e2e/<name>.xl`, a single e2e entry). Run each full suite once at the very end; fix what it finds, otherwise finish. Write and modernize tests to be fast (small inputs, no sleeps, no needless Docker/network).

## Std API types (hard rule)
- Stdlib functions take and return typed values: `String` for owned text, `struct`/`class` for records, `Vec<T>` for lists. Raw `*T` pointers and the `str` view appear only where the value is a byte buffer at a boundary (syscalls, FFI, the internals of a type), never as the public return type of text.
- Names carry no module prefix. A function imported from `std/str` is `slice`, `eq`, `split`, `replace`, not `strSlice`; the import already names the module. Overloads and methods use the same rule.
- Where a conversion between the built-in `str` view and a typed form is needed, the compiler may perform it; until it does, the stdlib provides one conversion function per direction, not one API per representation.
- A new public function that takes text takes `String` or `str`, and a new one that returns text returns `String`, unless a byte buffer is the honest type.

## Std layout (hard rule)
- `std/` is nested by area: `std/<area>/<file>.xl` with `std/<area>/index.xl` re-exporting the area (`"runtime"` -> `std/runtime/index.xl`; siblings import as `"name"`, parents as `"../name"`). Deeper nesting is allowed when an area grows.
- New stdlib code goes into the matching area directory; a new top-level file is only for a new area. `std/runtime/`: `panic`, `unwind` (panic unwinding, `register(unwind)`), `context` (stack switching).
- Network area (planned layout): `std/sys/` (native OS layer, see Native OS code), `std/net/` (IPv4 and IPv6 address types, sockets, DNS, all network primitives; `std/dns` moves here), `std/tls/` (TLS 1.0 to 1.3 and SSL 3 as separate, versioned parts), `std/http/` (HTTP/1.1 on `std/net`; HTTPS uses `std/tls`), `std/crypto/` (hashes, MACs, ciphers, key exchange). Each area has an `index.xl`.