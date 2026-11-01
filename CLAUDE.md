# xlang — notes for Claude

## Platforms (hard rule)
- Write new code for Linux and macOS only (x86-64 and arm64). Do not write Windows code or Windows-specific branches for now; where a feature has no Windows support, it is a clear compile-time error or a documented gap in `KNOWN-LIMITATIONS.md`, not a half-written port.
- Windows support (verified under Wine/CrossOver or real Windows) comes later as its own step. Existing Windows code stays as is; do not extend it, and new e2e entries get Windows column `0`.

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

## Docs
- `KNOWN-LIMITATIONS.md`: open decisions and intentional incompleteness.
- `src/README.md`: layout. `std/`: stdlib. `test/e2e/`: end-to-end programs.
- `docs/`: human docs (English, accurate to implementation).
- `docs-ai/`: AI docs (matching topics). Format: title + 5–10 plain one-line sentences. Prose only. No code, examples, tables, lists, quotes, backticks, filler. Every sentence is a rule or behavior. Update both `docs/` and `docs-ai/` together.
- **Every code change must update the affected docs in the same change** (`docs/`, `docs-ai/`, `KNOWN-LIMITATIONS.md`, `src/README.md`, doc comments). A code update is not done until the docs match it.

## Core rules
- No compatibility layers. Language and compiler can change freely.
- Ignore `examples/`, `test/xlang/`, old `.xlang` files, and `.cursor/`.
- Maintain only `std/` (`.xl`) and `test/e2e/`.
- Compiler knows only the language, CPU primitives, and a closed builtin list (`syscall`, `__builtin_` atomics/fence/asm, `__builtin_read`/`__builtin_write`/`__builtin_drop`/`__builtin_needsDrop`, `__builtin_unwindPads`/`__builtin_unwindPadCount`, `sizeOf`-style, `staticAssert`, `nil`, `MemoryOrder`).
- Roles via `register`: `panic`, `alloc`, `free`, `unwind` (all functions). Bodies of builtins come from the compiler; everything else from stdlib.
- **Compiler level vs runtime level (hard rule).** The compiler owns language semantics, types, layout, CPU primitives and compile-time facts (`sizeOf`, `alignOf`, drop glue). Everything that happens at run time (memory acquisition, allocation policy, panic text/abort, I/O, threads, string/format machinery) is stdlib. The compiler must not know a runtime type, interface, struct or method by name (no `Allocator`, no `"alloc"`/`"free"` method lookup). The only compiler↔runtime seam is a `register(role)` function whose signature is checked structurally; a new runtime need becomes a new role function or stdlib code, never a name inside the compiler. Before adding a builtin, a role or a name check, ask whether stdlib can do it; if the compiler does need a hook, record it in `KNOWN-LIMITATIONS.md`.
- Entry symbols (`_start`, `mainCRTStartup`) and `_xl_initAll` are compiler-owned; user symbols with those names are `E0726`.
- `memcpy` family, `malloc`/`free`/allocator are **not** builtins. They live in stdlib (`std/memory`, `std/mem`).
- Compiler loads default stdlib but gives no special treatment to names inside it.
- **libc is forbidden.** Never use, call, imitate, or suggest it (or any OS C runtime).
- Native OS libraries allowed only when necessary, via `@library("name") declare fn`. Prefer raw `syscall`. Imports stay in `std/sys/<os>.xl` behind `#if os`. No shared C runtime across OSes.
- Do not invent features. Record decisions in `KNOWN-LIMITATIONS.md` or ask.
- File changes only with Read/Edit/Write tools. Bash is for build/run/inspect only.

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
- Packages: git repositories of `.xl` source (`docs/PACKAGE_MANAGER.md`). `xlang.toml` + `xlang.lock` + `.xlang/cache`; `xlang package install|update|remove|list|clean`; builds never use the network. Release tags `v?X.Y.Z`; `latest` = highest stable tag only (no fallback); `-suffix`/`-test` tags install only when named (`repo@v1.2.0-test`). No registry, no required layout, no solver. The compiler knows only "import path under a dependency path -> directory" and `[[native]]` link inputs.
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

## Std layout (hard rule)
- `std/` is nested by area: `std/<area>/<file>.xl` with `std/<area>/index.xl` re-exporting the area (`"runtime"` -> `std/runtime/index.xl`; siblings import as `"name"`, parents as `"../name"`). Deeper nesting is allowed when an area grows.
- New stdlib code goes into the matching area directory; a new top-level file is only for a new area. `std/runtime/`: `panic`, `unwind` (panic unwinding, `register(unwind)`), `context` (stack switching).