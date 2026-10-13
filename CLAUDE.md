# xlang — project notes for Claude

## Language rules

- **Everything written to the repo is in English**: source code, comments, docs, commit messages, this file,
  diagnostics text produced by the compiler.
- **Exceptions: everything under `plan/` and `plan/eg/` stays in Turkish.**
- Chat replies to the user stay in Turkish (the user writes Turkish).

## Plan files (read these first)

| File | Contents |
|------|----------|
| `plan/` | Compiler plan, one file per section (`NN-*.md`): rules, decisions, pending decisions (`03-karar-bekleyen-*.md`), phases, PR breakdown; index in `plan/README.md` |
| `plan/eg/` | What the language looks like once the plan is done, one file per feature (`NN-*.md`, `NN.M-*.md`): ASCII tables, xlang code examples; index in `plan/eg/README.md` |

### Sync rule

- If a plan change affects the **language surface** (syntax, types, features, CLI flags, import forms,
  attributes, builtins), update **`plan/eg/` in the same change**.
- If only compiler internals change (IR, passes, codegen), update only `plan/`.
- The user usually drives plan updates through `plan/eg/`. When adding a feature, write the example first
  (description + ASCII table + `xlang` code block) in `plan/eg/`, then add the item to `plan/`.
- Every feature in `plan/eg/`: its own file, one heading, a short description, a table where useful, an `xlang` code
  example. New features get a new file and a line in `plan/eg/README.md`; likewise new plan sections in `plan/README.md`.

## Working rules

- **No backward or forward compatibility.** The compiler has never been used; syntax, AST, CLI and IR can be
  rewritten from scratch. Do not write compatibility layers.
- **Do not read or adapt to `examples/`, `test/xlang/`, or stdlib/runtime `.xlang` files.** They are disposable
  and will be rewritten once the compiler reaches its target.
- **Do not read anything under `.cursor/`** (rules or plans).
- The compiler knows only the language, CPU primitives (syscall, atomics, function pointers, inline asm), the
  closed builtin list (`syscall`, the `__builtin_` atomics, fence and asm; `sizeOf`-style queries,
  `staticAssert`; `MemoryOrder`) and the roles bound with
  `register` (`panic`, `allocator`). A builtin's body is provided by the compiler itself; if the compiler will not
  directly support something, it is not a builtin and goes to the stdlib. `memcpy` family, `malloc`/`free`/the
  allocator are NOT builtins: they come from the stdlib (`std/memory`, `std/mem`). The compiler loads the default
  stdlib (a driver decision: `--no-std` / `--std=<path>` change it) but never gives special treatment to any name,
  type or function inside it (no precedence rules; roles bind only via `register`). Thread, scheduler and print concepts do not belong in the
  compiler; they are xlang stdlib.
- **libc is forbidden**, absolutely: never used, never called, never imitated. Do not suggest it, do not put it in
  examples (`puts`, `FILE`, `environ`, ...).
- Do not invent features or design beyond what the user asked for. If a decision is needed, record it in
  the "pending decisions" file in `plan/` (`03-karar-bekleyen-*.md`) or ask.
- Use the default tools (Read/Edit/Write) for file changes; use bulk regex/python/sed only when necessary.

## Feature assessment (applies in every session)

When the user adds a feature or decision to the plan, give an **honest assessment** alongside the plan update.
No filler praise; state weaknesses plainly. For each feature cover:

| Heading | Content |
|---------|---------|
| Impact | Cost to the compiler: complexity (low/medium/high), phases touched, maintenance load |
| Value | Benefit to users/the language: huge / good / marginal / junk (with reasoning) |
| Comparison | Which compiler/language it is better or worse than (clang/gcc, Rust, Zig, Go, Swift, Odin, ...); concrete differences |
| Risk | Known pitfalls, conflicts with other features, places that will become expensive later |
| Recommendation | Now, later, or a reduced version; a short alternative if there is one |

- If you see a conflict or a bad idea, do not just accept it: object, but leave the decision to the user and apply it.
- Cite real-world precedent concretely (other languages that tried the same path or backed out of it).
- Do not re-ask topics the user closed with "do not ask again" (e.g. external libraries/libc); if needed, give a
  one-time risk note instead.

## Language decisions (summary; details in `plan/eg/`)

- Functions: `fn name(a: i32) -> i32 { }` (no `->` means `void`).
- Naming (no snake_case in xl names): types are `PascalCase` (`Point`, `Allocator`, `MemoryOrder`); functions, methods,
  variables, fields and builtins are `camelCase` (`setAllocator`, `sizeOf`, `staticAssert`, `MemoryOrder.seqCst`);
  the `__builtin_` prefix stays and the rest is camelCase (`__builtin_atomicLoad`, `__builtin_asm`). Names that
  come from outside xl keep their own spelling (C/OS symbols in `declare`, `uint8_t`, `x86_64`, CLI flags, std
  feature names like `default-allocator`).
- Types: `i8 i16 i32 i64 isize`, `ui8 ui16 ui32 ui64 usize`, `f32 f64`, `bool`, `void`, `*T`, `fn(A) -> R`,
  `[T; N]` (fixed-size array value type, `N` a compile-time constant; literals `[a, b]` / `[v; N]`; `a[i]` is
  bounds-checked under `--safety=checked`, default, calling `panic`; `p[i]` on `*T` is unchecked).
- `enum Name: T { A, B = 5 }`: `T` is any type that can hold a constant (integers, floats, bool, `*ui8` strings);
  defaults to `i32`; access `Name.A`; conversions with explicit `as`. `MemoryOrder` is a builtin enum.
- Diagnostic codes: `W` = warning, `E` = error (e.g. `E0302` non-C type in `export(c)`, `E0501` no allocator for
  `new`, `E0502` duplicate `register`, `E0503` role contract not met, `E0504` interface used without `impl`, `W0402` local name shadows a registered one,
  `E0601` implicit signedness-changing conversion, `W0601` implicit signed int -> float conversion).
- Implicit conversions (no `as` written): same-signedness integer widening is implicit; signed integer -> `f32/f64`
  is implicit with `W0601` (silence with `as`, `@allow(W0601)` on a statement/block/fn/file, or `--allow`);
  unsigned <-> signed implicit is error `E0601` for every type, floats included (floats count as signed, so
  `ui -> f` is `E0601`); allowed only inside an `unsafe { ... }` block (same or wider target; narrowing always
  needs `as`). `unsafe` relaxes nothing else (`--safety`, `*T`, `ref T` are unaffected). Everything else is
  explicit `as`.
- `register(role) decl` / `register decl`: a single prefix like `declare`, opposite direction: the declared name
  becomes **global** (no import) as soon as the module enters the compilation, and implies `export`. Roles:
  `panic` (`fn(msg: *ui8)`, noreturn; the compiler's inserted failure paths — division by zero, array bounds,
  `ref` generation mismatch — call it, plain trap if unregistered) and `allocator` (a mutable global whose type is
  the `Allocator` interface or a type with `impl Allocator for T`; `new T` calls its `alloc`). One registration per role. Registered names are
  shadowed (with `W0402`) by local definitions. `panic` lives in `std/runtime`, `assert!` in `std/assert`.
- Memory layout: default xlang layout; `@repr(c)` gives C/C++ layout.
- `class` (single inheritance, vptr) and `interface` (fat pointer `{data, itable}`) are compiler features.
- Conditional compilation: line-based `#if/#elif/#else/#endif/#ifdef/#ifndef/#error`; the condition is the normal
  `if` expression syntax; body on the same line means no `#endif`, body on a later line requires `#endif`;
  inactive regions are only lexed. CLI: `-D`, `-U`, `--target`, `-O`, `-g`.
- Type alias: `type Name = T` (transparent). Macros: `define N = const`, `define f(x: expr) { ... }`, call `f!(...)`;
  brace-delimited body, no `\` and no `#`/`##`, typed parameters, hygienic. There is no `stringify!`.
- Source extension `.xl`. No default export; `export` (between xl modules) and `export(c)` (unmangled C symbol).
- Name mangling is on by default; overrides: `--mangle=`, `@file(mangle = ...)`, `@mangle(...)` (fn > file > project).
- `new T { fields }` and `new(a) T { fields }`: allocate via `alloc(self, size, align) -> *void`, initialize, return
  `*T`. An allocator is the `std/mem` `Allocator` interface (`alloc` + `free`): a type must have an explicit
  `impl Allocator for T` (interfaces are nominal; matching method names never count, missing impl is `E0504`),
  and freeing always goes to the same allocator. `new T` uses the `register(allocator)` global (the compiler looks for the registration, not a name, and
  does not know `malloc`); at most one registration in the whole program. The stdlib (`std/mem`) registers
  `currentAllocator` under the std feature `default-allocator` (on by default); a user either calls
  `std/mem.setAllocator(a)` once at the start of `main` (set-once, second call panics; `new` and `free` follow)
  or turns the feature off with `--std-features=-default-allocator` and registers their own. With no
  registration, `new T` is error `E0501` (use `new(a) T`). `sqrt` and math come from `std/math`. The relation of
  `new` to `ref T` is a pending decision.
- Global variable initializers are compile-time constants (static init, no runtime init order): literals, `define`
  constants, enum members, `sizeOf`-style queries, `&global`, `&global[const]`, `as` casts, constant struct/class/
  array literals; no function calls or reads of other globals. No initializer means zero-initialized.
- Safety: `*T` unchecked, `ref T` generation-checked (`--safety=checked|unchecked`); no borrow checker.
- Outputs: `--emit=obj|ir|bc|header|exe|static|shared`, default `exe`. The compiler produces every output itself and
  runs the rest of the pipeline to the end by default (calling LLVM + linker/archiver when needed); `ir`/`bc`/`obj`/
  `header` stop at that stage. It links only its own output (no libc/external libs). Linking into a C++ project is
  the user's build script (xmake): take `--emit=obj`.
- Atomics are builtins with the `__builtin_` prefix: `__builtin_atomicLoad/Store/Xchg/Cas/Add`, `__builtin_fence`.
  The order argument is a compile-time `MemoryOrder` member (`MemoryOrder.seqCst`). Inline asm is
  `__builtin_asm("template", in(reg) x, out(reg) y, clobber("rcx"), options("noreturn"))` (syntax in
  `plan/eg/12.1-inline-asm.md`): the template is target-specific and chosen with `#if arch` / `#if os`, Intel dialect on
  x86, memory is assumed touched unless `options("nomem")`.
  `mutex` exists in the stdlib (`std/sync`, built on the atomics + OS wait); there is no `lock_guard`/RAII guard.
- Global namespace has two tiers: (a) the closed builtin list: `syscall`, atomics/`fence`/asm, `sizeOf`-style
  queries, `staticAssert`, the `MemoryOrder` enum; (b) names bound with `register` in any loaded module.
  Builtins come in two spellings: `__builtin_xx` (atomics, fence, asm) and unprefixed (`syscall`, `sizeOf` family,
  ...); not all carry the `__builtin_` prefix. `syscall` is always plain `syscall`. `register`'d names are not
  builtins (their body is ordinary xl). `print`, `alloc`, `memcpy`, `assert` and anything neither builtin nor
  registered come from the stdlib via import.
- `declare` is a single prefix: `declare fn/global/struct/class/interface/type`; no special global syntax; attributes
  (`@repr(c)`, `@mangle`, ...) are the same as for normal declarations; it is only for things coming from other
  objects/libraries.
- No libc: xl has its own stdlib; `memcpy/memset/memmove/memcmp` (LLVM emits implicit calls)
  are written in xl in the stdlib and exported with `export(c)`; `alloc/free` live in the stdlib too.
- The language takes no external libraries (no libc, libSystem, kernel32, pthread library). The stdlib calls each
  OS's kernel ABI with raw `syscall` + `#if os` (including macOS/Windows; stability is not a concern). Do not ask again.
- Stdlib is optional: `--no-std`, `--std=<path>`; the default stdlib is embedded and contains a Go-like M:N scheduler.
- The inline asm builtin is mandatory (thread trampoline, scheduler context switch).
- Two exports or two imports binding the same name is an error (reported on the import/export line):
  `import * from a` + `import * from b` with a common name is an error; `import * as aa from a` +
  `import * as bb from b` is fine; the same entity arriving via two paths is not an error but a detailed
  warning (`W0401`) plus a log entry. Module resolution strategies are not planned yet.
- `@repr(c)` is not required on `declare`; layout resolution order: layout record in the object -> `xl.layout`
  history -> C assumption + warning. Layout schemes are versioned and frozen (v1, v2, v3...); objects carry a
  layout record.
- Diagnostics: the compiler does not restrict, it warns (`what + detail + reason + how to fix`); coded, with
  `--allow/--deny/@allow/--explain`. The faulty spot is shown at the end of the line with `<======= pinpoint comment`
  (both sides of a conflict); logged with `--diag-log=<file>`. Context lines: error -10/+10, warning -2/+2
  (`--context-errors/--context-warnings/--context`); line numbers right-aligned.
- Module path: `"x"` -> `x.xl` or `x/index.xl`. Re-exports: `export * from "m"`, `export * as ns from "m"`,
  `export { a, b as c } from "m"`, `export * from "m" except a`. `import "std"` provides everything.
- Import: both `from "m" import x` and `import x from "m"` are valid; also `import * from "m"` and
  `import * as a from "m"`.
- Syscall: builtin `syscall(nr, args...)`. The argument count is dynamic: the compiler forwards exactly the number
  given, with no fixed limit in the language. The per-target maximum (from the target OS/arch table) is checked
  at compile time and exceeding it is a compile error. Do not write "0-6" or similar fixed counts anywhere.
  The return is always `i64` with errors as negative `-errno` on every target (macOS carry flag is normalized by
  the compiler; Windows returns the NTSTATUS sign-extended).

## Build

- Build system is xmake: `xmake build`; output `build/xlang`.
- `xlang parse <file> [--ast]`, `xlang check <file>`, `xlang layout <file> [Type]`, `xlang --explain <CODE>`;
  build options `-D/-U/--target/-O/-g/--print-cfg` (frontend only, no codegen yet).
- Unit tests: `xmake build xlang-tests && xmake run xlang-tests` (`test/compiler/`).
- The old codegen/compiler/host/platform sources, `examples/` and `test/xlang/` were deleted; codegen returns in
  plan phase 4.7 (see `src/README.md`).
