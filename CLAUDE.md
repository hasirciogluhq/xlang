# xlang — notes for Claude

## Language of work
- Repo content is English (code, comments, docs, commits, diagnostics). User chat is Turkish.

## Repositories
- This repository is the `xl` tool, written in xlang (`src/`, layout in `src/README.md`), plus its tests (`test/run.sh`), CI and install scripts.
- Submodules, each an independent repo: `xlc/` (the compiler), `xl-std/` (the stdlib), `xlang-docs/` (the documentation site), `xl-vscode/` (the VS Code extension). Changes are committed and pushed inside the submodule first, then the pointer is committed here.
- Never add `Co-Authored-By` trailers or any "Generated with Claude Code" line to commits or PR descriptions, here or in any submodule.

## Platforms
- Every feature of `xl` and the stdlib is written for Linux, macOS and Windows (x86-64 and arm64 where the OS supports it). Where an OS cannot support a feature, it is a clear compile-time error or an entry in `xlang-docs/known-limitations.md`, never a half-written port.
- A Windows column of `0` in the e2e table means not yet verified under Wine/CrossOver or on real Windows, not out of scope.

## xlang code style (hard rule: `src/` and `xl-std/`)
- **The stdlib is a pyramid.** The compiler owns `str`, slices, arrays and pointers. The stdlib builds `String`, `Vec<T>`, `Option`, `Result` on them, and everything above (the rest of the stdlib and `xl`) uses those types and the stdlib's own functions. No raw `*u8` + length where a `String`/`str` fits, no hand-written copy loop where a stdlib function exists. Raw pointers and `str` appear only at a real boundary (syscalls, FFI, the internals of a base type).
- **Modern and compact.** Use the newest syntax the language has: `for i in 0..n`, the iterating `for x, i in s`, ternary, compound assignment, `switch`, `defer`, struct literals, tuples, generics, destructuring, closures, `?`, `impl<T>` blocks. No `while` for counted loops, no byte-by-byte code where a helper or a `str`/`String` operation does it, no repeated boilerplate. Write less; the code must read at a glance and be easy to extend.
- **When the language lacks a form the code needs**, say so to the user so it can be added to the language, then write the code. Do not work around it with clumsy code; record the gap in `xlang-docs/known-limitations.md`.
- **Std API types.** Stdlib signatures (public and internal) never use `str`, `*u8` + length, or offset pairs for text and bytes: they take `*String` to borrow and return `String` to give, `struct`/`class` for records, `Vec<T>` for lists. A packet or buffer is a `String`. `str` appears only inside `String` and the other base types themselves, and for literals handed to them. A missing `String` operation is added to `String`, not worked around with `str`.
- **Names carry no module prefix** (`slice`, `eq`, `split`, not `strSlice`); the import names the module.
- **Stdlib layout.** `xl-std/<area>/<file>.xl` with `xl-std/<area>/index.xl` re-exporting the area (siblings import as `"name"`, parents as `"../name"`). New code goes into the matching area; a new top-level directory is only for a new area. `src/` follows the same shape (`core/`, `pkg/`, `lc/`, `build/`, `self/`).
- **Comments are English and dense**: explain each non-obvious layout, flag, errno and OS difference, and cite the specification (`https://www.rfc-editor.org/rfc/rfcNNNN`, man pages or vendor docs as URLs).
- **File size.** A file is too big when one edit forces reading much unrelated code, when it has more than one responsibility, or when it no longer fits one Read (roughly 1000 lines). Split it into cohesive files first as a pure move, update the layout docs, then make the change.

## Native OS code
- OS code (syscalls, ntdll/kernel32/ws2_32 imports, handles) lives in `xl-std/sys/`: one file per area (socket, process, file, memory, entropy), routed with `#if os`. Split per OS only when the bodies share nothing. One function for all OSes when the arguments and result match; route with `#if` at the smallest scope.
- **Docs first.** Before writing or changing a syscall or OS call, read the official docs for every target OS and record the numbers, struct layouts and error behavior in `xlang-docs/` first. Never guess.
- **OS setup is hidden.** Per-OS one-time state (WSAStartup, SIGPIPE) never reaches the user API; each area has one internal `…Up()` function called by every public creator.
- A Windows shared library gets a `DllMain` (`DLL_PROCESS_ATTACH` runs `_xl_initAll`, `DLL_PROCESS_DETACH` runs `_xl_finiAll`); the module handle comes from a `xl-std/sys` helper.
- OS runtime libraries may be linked (`@library("name") declare fn`) on all three OSes. Only stdlib calls them; user code reaches them through stdlib functions. Prefer raw `syscall` where the number is documented and stable; use `@library` where the OS promises the interface through its library (macOS `kevent`).

## Compiler vs runtime (hard rule)
- The compiler (`xlc/`) owns language semantics, types, layout, CPU primitives and compile-time facts (`sizeOf`, `alignOf`, drop glue). Everything at run time (memory, panic text, I/O, threads, strings, formatting) is stdlib. The compiler must not know a runtime type, interface or method by name. The only seam is a `register(role)` function checked structurally; a new runtime need becomes a new role or stdlib code. If the compiler needs a hook, record it in `xlang-docs/known-limitations.md`.
- Closed builtin list: `syscall`, `__builtin_` atomics/fence/asm, `__builtin_read`/`__builtin_write`/`__builtin_drop`/`__builtin_needsDrop`, `__builtin_unwindPads`/`__builtin_unwindPadCount`, `sizeOf`-style, `staticAssert`, `nil`, `MemoryOrder`. `memcpy`, `malloc`/`free`, allocators are stdlib (`xl-std/mem`).
- Entry symbols (`_start`, `mainCRTStartup`) and `_xl_initAll` are compiler-owned; user symbols with those names are `E0726`.
- `register(role) decl` makes a name global and exported. Roles: `panic`, `alloc`, `free`, `unwind`. One per role per group; group 1 is the stdlib default, a group 2 (user) registration replaces it.
- Only reachable code is linked.

## Docs (single source)
- `xlang-docs/` is the only documentation, published as the GitHub Pages site (Astro Starlight): `language/`, `compiler/`, `std/` and `known-limitations.md` under `src/content/docs/` (open decisions and intentional incompleteness only). Plain, professional Markdown. A new page goes into the `sidebar` of `xlang-docs/astro.config.mjs` and into `xlang-docs/src/content/docs/index.md` in the same change; check with `bun run build` there.
- **Every code change updates the affected docs in the same change** (`xlang-docs/`, `src/README.md`, doc comments). `xl` itself is documented in `compiler/cli.md`, `packages.md` and `install.md`.

## Core rules
- **No backward compatibility on `development`.** When behavior changes, the old form is removed completely from code, tests and docs, with no note about it. No compatibility layers.
- Do not invent features: record decisions in `xlang-docs/known-limitations.md` or ask. Do not re-ask closed topics.
- **File edits use the built-in Read/Edit/Write tools, always.** No `python`, `sed`, `awk`, `perl` or shell redirection to replace or write text in a file. The only exception is a mechanical bulk update that touches the same import/export line in many files (a module rename). Bash is for build, run, inspect, and moving or deleting whole files.

## Feature assessment
When the user adds a feature, give an honest assessment without praise: impact (complexity, phases), value (huge / good / marginal / junk, with reason), comparison (Rust, Zig, Go, Swift, Odin, C), risk, and a recommendation (now / later / reduced). Object to bad ideas with real precedents, then leave the decision to the user.

## Language summary
- Functions `fn name(a: i32) -> i32 { }` (no `->` = void). Types `PascalCase`; functions, vars, fields `camelCase`; `__builtin_` prefix stays.
- Types: `i8..i64`, `u8..u64`, `f32/f64`, `bool`, `void`, `*T`, `fn(A)->R`, `[T; N]` (bounds-checked under `--safety=checked`), `[]T`/`[]mut T` slices, `str`, tuples, `enum Name: T`, payload and generic enums. Conversions need `as`, except same-signedness integer widening; unsigned ↔ signed is `E0601`.
- Control: `while`, `do {} while`, `for` (C-style, `for i in lo..hi`, `lo..=hi`, `for x, i in s`), labeled `break`/`continue`, `switch` (no fallthrough), `defer`, `try { } catch e { }` (needs the `unwind` role, `E0501` otherwise), ternary, compound assignment, `x++`/`x--`, `?` on `Option`/`Result`. Closures `|x| expr` and `move |x| { }` (a capturing closure is passed as a generic `F`). Generics on fn, struct, class, interface, enum, alias and method (monomorphized, no constraints); `impl<T> Type<T> { }` adds methods; operator functions `fn +`, `fn ==`, `fn []`, `fn []=`, `fn ()`.
- `struct` is plain data (a droppable field is `E0721`); `class` has methods, single inheritance and a vptr; `interface` is a fat pointer. `@repr(c)` gives C layout.
- Memory: `new T` uses the registered `alloc`, `new(p) T` places into `p`, `delete p` drops then calls `free`. Globals take constant initializers only. `__builtin_read(p)` / `__builtin_write(p, v)` move raw through a pointer.
- `@init fn name() {}` is a module-level boot function (stdlib group first, then user; imports before importers). `@entry`, `@library`, `declare`, `export`/`export(c)`, `#if/#elif/#else/#endif`, `type Name = T`, hygienic macros (`define f(x: expr) { }`, `f!(...)`).
- Modules: `"x"` → `x.xl` or `x/index.xl`; `"NAME/x"` inside the root `-M NAME=DIR`; `from "m" import x` and `import x from "m"`; `import *`; re-exports. Two exports of one name are an error; the same entity via two paths warns `W0401`.
- `syscall(nr, args...)` returns `i64` (`-errno` style); the target's argument maximum is checked at compile time.
- Diagnostics: `E` error, `W` warning, with a code and how to fix; `--allow`/`--deny`/`@allow`/`xlc --explain`.
- The stdlib is a module root (`xlc -M std=<dir> --import std`); features are definitions that become `featureXxx` constants (`-DfeatureDefaultAllocator=true`). Packages are git repositories of `.xl` source (`xlang-docs/compiler/packages.md`): `xlang.toml`, `xlang.lock`, `xlang.local.toml`, `.xlang/cache`; builds never use the network.
- `xlc --emit=obj|ir|bc|header|exe|static|shared`. `xl run|build|check|env|pkg|lc|self-update|version` (`xlang-docs/compiler/cli.md`).

## Build and test
- Build `xl`: `cd src && xlc main.xl -M std=../xl-std --import std -DfeatureDefaultAllocator=true '-DxlVersion="1.0.0-rc.1"' -o <out>`. Test it: `test/run.sh <xl> <xlc> xl-std` (every command, no network). The compiler `xlc` is built in `xlc/` or installed with `xl lc install`.
- Type-check the stdlib with `xlc xl-std/index.xl --check -DfeatureDefaultAllocator=true`. Its tests live in `xl-std/test/`: `xl-std/test/unit/run.sh [xlc]` and `xl-std/test/e2e/run.sh [xlc]` (native, Docker, CrossOver for Windows).
- **Unit tests are platform independent, e2e tests are platform related (hard rule).** `test/unit/*.test.xl` checks code that gives the same result on every OS and CPU: `Vec`, `String`, `Option`, slices, parsing, math, encodings, collections, control flow. It never touches the OS (no files, sockets, processes, clocks, threads, environment). `test/e2e/` holds what depends on the OS or the CPU: sockets, processes, files, threads, entropy, TLS and HTTP, syscalls, the Windows leg. The bulk of the tests are unit tests; the e2e table stays the small, heavy part.
- **Test order.** While working, run only the tests of what changed (`xlc xl-std/test/e2e/<name>.xl -M std=xl-std --import std -DfeatureDefaultAllocator=true -o /tmp/t && /tmp/t`). Run each full suite once at the very end and fix what it finds. Tests are small and fast: no sleeps, no needless Docker or network.
- CI (`.github/workflows/ci.yml`) builds `xl` and runs `test/run.sh` on Linux and macOS (x86-64, arm64) and Windows x86-64 with the newest `xlc` release; a `v*` tag runs `release.yml`.
