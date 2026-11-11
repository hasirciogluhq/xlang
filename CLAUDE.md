# xlang — notes for Claude

## Language of work
- Repo content is English (code, comments, docs, commits, diagnostics). User chat is Turkish.

## Platforms
- Every std feature is written for Linux, macOS and Windows (x86-64 and arm64 where the OS supports it). Where an OS cannot support a feature, it is a clear compile-time error or an entry in `xlang-docs/known-limitations.md`, never a half-written port.
- A Windows column of `0` in the e2e table means not yet verified under Wine/CrossOver or on real Windows, not out of scope.

## Stdlib style (hard rule)
- **The stdlib is a pyramid.** The compiler owns `str`, slices, arrays and pointers. The stdlib builds `String`, `Vec<T>`, `Option`, `Result` on them, and everything above uses those types and the stdlib's own functions. A stdlib file never reaches past a layer it already has: no raw `*u8` + length where a `String`/`str` fits, no hand-written copy loop where a stdlib function exists. Raw pointers and `str` appear only at a real boundary (syscalls, FFI, the internals of a base type).
- **Modern and compact.** Use the newest syntax the language has: `for i in 0..n`, the iterating `for x, i in s`, ternary, compound assignment, `switch`, `defer`, struct literals, tuples, generics, destructuring. No `while` for counted loops, no byte-by-byte code where a helper or a `str`/`String` operation does it, no repeated boilerplate. Write less; the code must read at a glance and be easy to extend.
- **When the language lacks a form the code needs** (closures, `Vec` iteration, `?`), say so to the user so it can be added to the language, then write the stdlib for it. Do not work around it with clumsy code.
- **Std API types.** Stdlib signatures (public and internal) never use `str`, `*u8` + length, or offset pairs for text and bytes: they take `*String` to borrow and return `String` to give, `struct`/`class` for records, `Vec<T>` for lists. A packet or buffer is a `String`. `str` appears only inside `String` and the other base types themselves, and for literals handed to them. A missing `String` operation (view, slice, index, compare) is added to `String`, not worked around with `str`.
- **Names carry no module prefix** (`slice`, `eq`, `split`, not `strSlice`); the import names the module.
- **Repository.** The stdlib lives in the `xl-std/` git submodule (`hasirciogluhq/xl-std`), an independent repo that only stands as a submodule here and references nothing of this repo. Stdlib changes are committed and pushed inside `xl-std/` first, then the submodule pointer is committed here. Commits in `xl-std` never carry `Co-Authored-By` or "Generated with Claude Code" lines.
- **Layout.** `xl-std/<area>/<file>.xl` with `xl-std/<area>/index.xl` re-exporting the area (siblings import as `"name"`, parents as `"../name"`). New code goes into the matching area; a new top-level file is only for a new area.
- **Comments are English and dense**: explain each non-obvious layout, flag, errno and OS difference, and cite the specification (`https://www.rfc-editor.org/rfc/rfcNNNN`, man pages or vendor docs as URLs).

## Native OS code
- OS code (syscalls, ntdll/kernel32/ws2_32 imports, handles) lives in `xl-std/sys/`: one file per area (socket, process, file, memory, entropy), routed with `#if os`. Split per OS only when the bodies share nothing. One function for all OSes when the arguments and result match; route with `#if` at the smallest scope.
- **Docs first.** Before writing or changing a syscall or OS call, read the official docs for every target OS and record the numbers, struct layouts and error behavior in `xlang-docs/` first. Never guess.
- **OS setup is hidden.** Per-OS one-time state (WSAStartup, SIGPIPE) never reaches the user API; each area has one internal `…Up()` function called by every public creator.
- A Windows shared library gets a `DllMain` (`DLL_PROCESS_ATTACH` runs `_xl_initAll`, `DLL_PROCESS_DETACH` runs `_xl_finiAll`); the module handle comes from a `xl-std/sys` helper.
- OS runtime libraries may be linked (`@library("name") declare fn`) on all three OSes. Only stdlib calls them; user code reaches them through stdlib functions. Prefer raw `syscall` where the number is documented and stable; use `@library` where the OS promises the interface through its library (macOS `kevent`).
- Only reachable code is linked.

## Compiler vs runtime (hard rule)
- The compiler owns language semantics, types, layout, CPU primitives and compile-time facts (`sizeOf`, `alignOf`, drop glue). Everything at run time (memory, panic text, I/O, threads, strings, formatting) is stdlib. The compiler must not know a runtime type, interface or method by name. The only seam is a `register(role)` function checked structurally; a new runtime need becomes a new role or stdlib code. If the compiler needs a hook, record it in `xlang-docs/known-limitations.md`.
- Closed builtin list: `syscall`, `__builtin_` atomics/fence/asm, `__builtin_read`/`__builtin_write`/`__builtin_drop`/`__builtin_needsDrop`, `__builtin_unwindPads`/`__builtin_unwindPadCount`, `sizeOf`-style, `staticAssert`, `nil`, `MemoryOrder`. `memcpy`, `malloc`/`free`, allocators are stdlib (`xl-std/mem`).
- Entry symbols (`_start`, `mainCRTStartup`) and `_xl_initAll` are compiler-owned; user symbols with those names are `E0726`.

## C++ (compiler code)
- C++23 minimum, C++26 wherever the compiler supports it. No C++20-or-older style.
- **Touch it, modernize it.** A change to a C++ file means tracing the touched code and its callers, callees, headers and tests and rewriting all of it to the latest level.
- Use the modern library idiomatically: concepts, `std::expected`, `std::optional`, `std::variant` + `std::visit`, `std::span`, `std::string_view`, ranges, `std::format`/`std::print`, structured bindings, `constexpr`/`consteval`, deducing `this`, `std::flat_map`, `std::generator`, `std::ranges::to`, `std::unreachable`, `[[nodiscard]]`, designated initializers, `enum class` + `std::to_underlying`.
- RAII and deliberate patterns (visitor, strategy, type erasure, builder, factory); no raw owning pointers, C arrays or casts, `new`/`delete`, C headers, out-parameters where a return value works, or macros where `constexpr`/templates work. Use `using` aliases, keep code DRY and short.

## File size
- No fixed limit: a file is too big when one edit forces reading much unrelated code, when it has more than one responsibility, or when it no longer fits one Read (roughly 1000 lines for dense code). Flat table-like files may be longer.
- Never edit a too-big file: first split it into cohesive parts (`src/<area>/<topic>_<part>.cpp` plus a shared internal header) as a pure move, update `src/README.md`, then make the change. New code goes into the matching part, never a catch-all.

## Docs (single source)
- `xlang-docs/` is the only documentation, published as the GitHub Pages site: `language/`, `compiler/`, `std/` and `known-limitations.md` under `src/content/docs/` (open decisions and intentional incompleteness only). Plain, professional Markdown.
- The site is Astro Starlight; pages are in `xlang-docs/src/content/docs/`. A new page goes into the `sidebar` of `xlang-docs/astro.config.mjs` and into `xlang-docs/src/content/docs/index.md` in the same change. `src/README.md` is the layout; `xl-std/` is the stdlib; `xl-std/test/e2e/` holds the only examples.
- **Every code change updates the affected docs in the same change** (`xlang-docs/`, `src/README.md`, doc comments).

## Core rules
- **No backward compatibility on `development`.** When behavior changes, the old form is removed completely from code, tests, examples and docs, with no note about it. No compatibility layers.
- Never add `Co-Authored-By` trailers or any "Generated with Claude Code" line to commits or PR descriptions, in this repo and its submodules (`xlc/`, `xlang-docs/`, `xl-std/`).
- Old `.xlang` files, `examples/` and `.cursor/` must not be recreated.
- `register(role) decl` makes a name global and exported. Roles: `panic`, `alloc`, `free`, `unwind`. One per role per group; group 1 is the stdlib default, a group 2 (user) registration replaces it.
- Do not invent features: record decisions in `xlang-docs/known-limitations.md` or ask. Do not re-ask closed topics.
- **File edits use the built-in Read/Edit/Write tools, always.** No `python`, `sed`, `awk`, `perl` or shell redirection to replace or write text in a file. The only exception is a mechanical bulk update that touches the same import/export line in many files (a module rename). Bash is for build, run, inspect, and moving or deleting whole files.

## Feature assessment
When the user adds a feature, give an honest assessment without praise: impact (complexity, phases), value (huge / good / marginal / junk, with reason), comparison (clang/gcc, Rust, Zig, Go, Swift, Odin), risk, and a recommendation (now / later / reduced). Object to bad ideas with real precedents, then leave the decision to the user.

## Language summary
- Functions `fn name(a: i32) -> i32 { }` (no `->` = void). Types `PascalCase`; functions, vars, fields `camelCase`; `__builtin_` prefix stays.
- Types: `i8..i64`, `u8..u64`, `f32/f64`, `bool`, `void`, `*T`, `fn(A)->R`, `[T; N]` (bounds-checked under `--safety=checked`), `[]T`/`[]mut T` slices, `str`, tuples, `enum Name: T`, payload and generic enums. Conversions need `as`, except same-signedness integer widening; unsigned ↔ signed is `E0601`.
- Control: `while`, `do {} while`, `for` (C-style, `for i in lo..hi`, `lo..=hi`, `for x, i in s`), labeled `break`/`continue`, `switch` (no fallthrough), `defer`, `try { } catch e { }` (needs the `unwind` role, `E0501` otherwise), ternary, compound assignment, `x++`/`x--`. Generics on fn, struct, class, interface, enum, alias and class method (monomorphized, no constraints).
- `struct` is plain data (a droppable field is `E0721`); `class` has methods, single inheritance and a vptr; `interface` is a fat pointer. `@repr(c)` gives C layout.
- Memory: `new T` uses the registered `alloc`, `new(p) T` places into `p`, `delete p` drops then calls `free`. Globals take constant initializers only. `__builtin_read(p)` / `__builtin_write(p, v)` move raw through a pointer.
- `@init fn name() {}` is a module-level boot function (stdlib group first, then user; imports before importers). `@entry`, `@library`, `declare`, `export`/`export(c)`, `#if/#elif/#else/#endif`, `type Name = T`, hygienic macros (`define f(x: expr) { }`, `f!(...)`).
- Modules: `"x"` → `x.xl` or `x/index.xl`; `from "m" import x` and `import x from "m"`; `import *`; re-exports. Two exports of one name are an error; the same entity via two paths warns `W0401`.
- `syscall(nr, args...)` returns `i64` (`-errno` style); the target's argument maximum is checked at compile time.
- Diagnostics: `E` error, `W` warning, with a code and how to fix; `--allow`/`--deny`/`@allow`/`--explain`.
- Stdlib is optional (`--no-std`, `--std=<dir|host/user/repo[@tag]>`, `[std]` in `xlang.toml`); features become `featureXxx` constants. Packages are git repositories of `.xl` source (`xlang-docs/compiler/packages.md`): `xlang.toml`, `xlang.lock`, `.xlang/cache`, builds never use the network.
- `--emit=obj|ir|bc|header|exe|static|shared`. CLI: `xl build|parse|check|layout|run|test|package|self-update|completions`.

## Build and test
- This repository has no build system (xmake removed): `xl` is the all-in-one tool, the compiler is `xlc` (built in the `xlc/` submodule), and the C++ in `src/` is being rewritten in xlang. Type-check the stdlib with `xlc xl-std/index.xl --check -DfeatureDefaultAllocator=true`.
- Tests live in `xl-std/test/` (`unit/` `*.test.xl` suites, `e2e/` programs + `tests.txt`) and run with the standalone compiler `xlc` (`-M std=xl-std --import std -DfeatureDefaultAllocator=true`; `xlc --help`). There is no C++ test suite. Suites: `xl-std/test/unit/run.sh [xlc]` and `xmake run e2e` / `xl-std/test/e2e/run.sh [xlc]` (native, Docker, CrossOver for Windows).
- **Unit tests are platform independent, e2e tests are platform related (hard rule).** `test/unit/*.test.xl` checks code that gives the same result on every OS and CPU: `Vec`, `String`, `Option`, slices, parsing, math, encodings, collections, control flow. It never touches the OS (no files, sockets, processes, clocks, threads, environment). `test/e2e/` holds what depends on the OS or the CPU: sockets, processes, files, threads, entropy, TLS and HTTP, syscalls, the Windows leg. The bulk of the tests are unit tests; the e2e table stays the small, heavy part. A test that needs the host goes to e2e, a logic check never does. The same rule holds in `xlc` (`xlc/CLAUDE.md`).
- **Test order.** While working, run only the tests of what changed (`xlc xl-std/test/e2e/<name>.xl -M std=xl-std --import std -DfeatureDefaultAllocator=true -o /tmp/t && /tmp/t`). Run each full suite once at the very end and fix what it finds. Tests are small and fast: no sleeps, no needless Docker or network.
