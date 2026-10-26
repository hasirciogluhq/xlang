# xlang — notes for Claude

## Language
- Repo content is English (code, comments, docs, commits, diagnostics). User chat is Turkish.

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
- Compiler knows only the language, CPU primitives, and a closed builtin list (`syscall`, `__builtin_` atomics/fence/asm, `sizeOf`-style, `staticAssert`, `nil`, `MemoryOrder`).
- Roles via `register`: `panic`, `allocator`. Bodies of builtins come from the compiler; everything else from stdlib.
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
- Control: `while`, `do {} while`, `for`, `for i in lo..hi`, labeled `break`/`continue`, `switch` (no fallthrough), `defer`, ternary, compound assignment, `x++`/`x--`. No generics/tuples yet.
- Naming: types `PascalCase`, functions/vars/fields `camelCase`. `__builtin_` prefix stays. External names keep original spelling.
- Types: `i8..i64`, `ui8..ui64`, `f32/f64`, `bool`, `void`, `*T`, `fn(A)->R`, `[T; N]` (fixed array). Bounds check under `--safety=checked` (default).
- `enum Name: T { ... }` (default `i32`). Access `Name.A`. Explicit `as` for conversions. `MemoryOrder` is builtin enum.
- Diagnostics: `E` error, `W` warning (e.g. `E0501`, `W0402`).
- Implicit conversions: same-signedness integer widening only. Signed int → float warns (`W0601`). Unsigned ↔ signed is error (`E0601`). `unsafe` allows some relaxations; narrowing always needs `as`.
- `register(role) decl`: makes name global + exported. Roles: `panic` (noreturn), `allocator`. One per role per group. Shadowing warns. Group 1 (stdlib) is the default; a group 2 (user) registration of the same role replaces it; a same-group duplicate is `E0502`; `register(start)` is `E0503`.
- `@init fn name() { }`: module-level boot function, no params, no return. Run order: stdlib group first, then user; per group, imports before importers, source order within a module. Calling it is `E0727`; bad signature or method use is `E0729`.
- `__builtin_setRegistered("role", value)`: replaces a registered global; only inside an `@init` fn (`E0728`).
- Memory: default xlang layout; `@repr(c)` for C layout. `class` (single inheritance + vptr), `interface` (fat pointer).
- Conditional: `#if/#elif/#else/#endif` (normal expression syntax). Inactive regions only lexed.
- `type Name = T` (transparent). Macros: `define`, `define f(x: expr) { ... }`, call with `f!(...)`. Hygienic, no stringify.
- Source: `.xl`. `export` (modules) and `export(c)` (unmangled). Mangling on by default.
- `new T` / `new(a) T`: allocate via registered `Allocator` or explicit one. `E0501` if no allocator. Stdlib registers default (`default-allocator` feature).
- Globals: compile-time constant initializers only. Zero-init if none.
- Safety: `*T` unchecked, arrays checked by default. No borrow checker.
- Emit: `--emit=obj|ir|bc|header|exe|static|shared`. Compiler runs full pipeline by default. Links only its own output.
- Atomics: `__builtin_atomic*` + `__builtin_fence`. Order is compile-time `MemoryOrder`. Inline asm: `__builtin_asm(...)`.
- Global namespace: closed builtins + `register`d names. `print`/`alloc`/`memcpy`/`assert` come from stdlib via import.
- `declare`: single prefix for external things. Same attributes as normal decls.
- Stdlib optional (`--no-std`, `--std=<path>`). Features become `featureXxx` constants for `#if`.
- Two exports/imports of same name = error. Same entity via two paths = warning `W0401`.
- Layout: `@repr(c)` not required on `declare`. Versioned layout records.
- Diagnostics: warn with code + how to fix. `--allow`/`--deny`/`@allow`/`--explain`. Context lines configurable.
- Modules: `"x"` → `x.xl` or `x/index.xl`. Re-exports supported. `import "std"` gives everything.
- Import syntax: both `from "m" import x` and `import x from "m"` valid. Also `import *`.
- `syscall(nr, args...)`: dynamic arg count (target max checked at compile time). Args integer or `*T` (auto-converted). Returns `i64` (`-errno` style).

## Build
- xmake. `xmake build`. LLVM from `llvm-config` (static link).
- CLI: `xlang build|parse|check|layout|run|test`, plus `--explain`, flags for defines/target/opt/debug/cpu/mangle/safety/std.
- Stdlib embedded at build time. `xmake build std` type-checks it.
- Tests: `xmake build xlang-tests && xmake run xlang-tests` (frontend/sema/HIR) and `xmake run e2e` (native + Docker + CrossOver for Windows).