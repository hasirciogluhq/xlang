---
name: Compiler Runtime Rebuild
overview: Decouple the compiler from syscall/link knowledge; CommonIrBuilder + TargetMachine; OS bridges with xl_* ABI; embed runtime/bridges in released xlang (two-phase bootstrap); --runtime=/--bridge= overrides; host/platform backbone.
todos:
  - id: host-platform
    content: src/host + platform/{linux,macosx,windows}; remove paths
    status: pending
  - id: strip-syscalls
    content: Blind declare + CPU-native syscall; OS baseline flags; embed runtime
    status: pending
  - id: native-syscall
    content: CPU-native syscall emit (arch opcode); runtime/ABI not required
    status: pending
  - id: os-bridges
    content: OS bridge folders; xl_<obj>_<action> ABI (e.g. xl_thread_start)
    status: pending
  - id: runtime-pkg
    content: Runtime embed in compiler; VERSION files; GitHub fetch; docs+rule
    status: pending
  - id: cli-overrides
    content: "--runtime/--bridge/--no-*; build=executable|static|shared|object"
    status: pending
  - id: bootstrap-embed
    content: "Two-phase bootstrap - compile runtime with stage1 xlang then re-embed"
    status: pending
  - id: link-policy
    content: OS syslib flag table + static embed; no non-OS externals by default
    status: pending
  - id: codegen-irbuilder
    content: CommonIrBuilder + TargetMachine; remove string .ll; no PlatformIrBuilder
    status: pending
  - id: cross-cli
    content: target/arch override flags + xmake defines
    status: pending
  - id: simplify-ast-lexer
    content: Simplify AST/lexer; leave broken if needed
    status: pending
isProject: false
---

# Compiler + Runtime Full Rebuild

## Fixed decisions

- **Syscall / call paths (both required):**
  1. **Blind declare/call** — `declare external` / `declare syscall` → LLVM `declare`+`call`. No whitelist. `pthread_create`, `xl_thread_start`, user stubs… all the same: blind. Link/embed resolves symbols.
  2. **CPU-native syscall** — user can reach the **kernel directly** without runtime/system ABI. Codegen emits the native opcode for the target arch (`x86_64` → `syscall`, `aarch64` → `svc`, …). Number + args come from the user; the compiler does not validate or memorize OS syscall tables. Works with `--no-runtime` and/or `--no-bridge`.
- **Bridge ABI naming (mandatory, xl helpers only):** `xl_<object>_<action>` — `xl_thread_start`, `xl_socket_connect`. Raw native syscalls and foreign symbols are not under this rule.
- **Codegen:** `AST → CommonIrBuilder → LLVM Module → TargetMachine(triple) → object`. No separate PlatformIrBuilder (does not emit OS IR). Platform tooling lives in `src/host` + `platform/*`; OS APIs live in runtime bridges.
- **OS work (bridges):** `bridge/linux|macosx|windows/`, with `thread.cpp` etc. and `#if` / arch defines for x86/x64. The same `xl_*` names are implemented on every OS.
- **Compiler:** Blindly compiles `declare`/`export`/`external`; **no** known-syscall list and **no** bridge `needs_*_link`. Link keeps: (1) target OS **baseline syslib flags**, (2) runtime/bridge material (default from **embedded** payload, or user override).
- **CLI overrides (main binary):** `--runtime=...` / `--bridge=...` replace that piece only. **`--no-runtime`** (must be set at build time): do **not** link/embed the xlang runtime; **default bridges still link** unless also disabled. **`--no-bridge`**: do not link default bridges either (raw compile—user supplies symbols/objects). Defaults extract embedded payloads to temp/build when not overridden/disabled.
- **Defaults for `xlang build name.xlang` (no extra flags):**
  | Setting | Default |
  |---------|---------|
  | Build type | `executable` (linked program) |
  | Runtime | **on** — extract/embed default runtime and link it |
  | Bridge | **on** — extract/embed default bridges and link them |
  | OS baseline syslibs | **on** for the target (e.g. `-pthread`) when linking an executable |
  | `--runtime=` / `--bridge=` | unset — use embedded (or in-tree/dev) defaults |
  | `--no-runtime` / `--no-bridge` | off — must be passed explicitly to strip that layer |
  | `--build=` | `executable` if omitted |
- **Output / build types:** Align with and extend [PACKAGE_MANAGER.md](docs/PACKAGE_MANAGER.md) / [LINKING.md](docs/LINKING.md):
  - **`executable`** (or `binary`) — linked program (**default**).
  - **`static`** — static library (`.a` / platform equivalent); first-class, full support.
  - **`shared`** — shared library (`.so` / `.dylib` / `.dll`); **experimental / limited support** (document constraints; not required to match static quality).
  - **`object`** — single object file (`.o` / platform equivalent); first-class type (not overloaded onto `lib`).
  Prefer an explicit build-type flag (e.g. `--build=executable|static|shared|object`) rather than inferring only from output extension. Compiler stays compatible; user owns anything beyond the requested type.
- **Release embed → extract → link:** At **compiler release** build, default bridge + runtime artifacts are **embedded into the `xlang` binary**. At user compile time the compiler **extracts** them into a temp or build directory, then links the user program against that material so it runs. Bridges (C) embed cleanly; **xlang frontend runtime needs a bootstrap dance** (below).
- **Bootstrap (the comedy):** You cannot embed `.xlang` runtime until something can compile it. So release build is two-phase: (1) build a normal stage-1 `xlang` without the final runtime embed → use it to compile the xlang runtime to `.o`/archive; (2) **throw away / rebuild** `xlang` from scratch with that runtime object **embedded** ready for extract-and-link. Bridges skip the joke (plain C). Document this as mandatory release procedure, not optional folklore.
- **Link policy:** Prefer **static embedding** into the **user program** (extracted runtime + `xl_*` bridges + user `.a`). **External/shared link only for OS syslibs**. Non-OS `.so`/`.dylib` not default.
- **Runtime:** Separate build artifact with `RUNTIME_VERSION` + `SUPPORTED_COMPILERS`; also the payload embedded in the released compiler. Distribution: GitHub Releases; PM CDN later. Compiler may still fetch/replace runtime when version policy says so—**user `--runtime=` always wins for that build**.
- **Completion:** Nothing deferred to “later”; every item in this plan is implemented. Breakage is acceptable; compile-tests are not required.

```mermaid
flowchart TB
  AST[AST] --> Common[CommonIrBuilder]
  Common --> Mod[LLVM_Module]
  Triple[target_triple] --> TM[TargetMachine]
  Mod --> TM
  TM --> Obj[object]
  Obj --> Link[Linker]
  RT[runtime_tarball] --> Link
  Bridge["bridge_os_xl_ABI"] --> RT
```

---

## 1) Bridge / frontend audit (fix during implementation)

**Excess / wrong layer:**
- In-compiler pthread/net send-recv/atomic IR (`syscalls.cpp`) → move to bridges or delete.
- `cwd/chdir` in both filesystem and process → keep one place (process).
- Inconsistent `xlang_` prefixes → flat domain names (filesystem model).

**Gaps (minimum to close with OS bridge folders):**
- net: send/recv/close in the bridge (currently in IR).
- thread/sync primitives in the OS bridge (currently in IR).
- windows folder + stub/impl.

**Frontend:** Protocols (http) stay in xlang; bridges only expose `xl_*` userspace helpers.

---

## 1b) `xl_*` naming rule (normative)

| Rule | Correct | Incorrect |
|------|---------|-----------|
| Prefix `xl_` | `xl_thread_start` | `thread_start`, `xlang_thread_start` |
| `<object>_<action>` | `xl_socket_listen` | `xl_listen_socket`, `xl_start_thread` |
| Underscored domain | `xl_thread_start` | `xlthread_start` |
| Bridge ≠ raw syscall | `xl_thread_start` (helper) | compiler knowing kernel `clone` numbers |

Raw syscalls / user `declare` names are **not** under this rule (passthrough). Only the **runtime bridge C ABI + frontend declares bound to bridges** use `xl_*`.

Same table goes into docs + a cursor rule.

---

## 2) Two paths: blind declare + CPU-native syscall

**Mindset:** The compiler does not keep an OS API catalog. The user either uses a userspace ABI (`pthread` / `xl_*`) or enters the kernel via a **CPU-native syscall**.

### Path A — Blind declare/call
- `declare syscall` / `declare external` → plain `declare` + `call`.
- No name validation, no body emission.
- Examples: `pthread_create`, `xl_thread_start`, a symbol from your own `.o`.

### Path B — CPU-native syscall (I do not want the runtime)
- Language/codegen feature: user supplies syscall number + args; codegen emits the **native instruction** for the arch.
- `x86_64`: `syscall` · `aarch64`: `svc #0` · other archs in the target table.
- LLVM: inline asm or arch intrinsic — **CommonIrBuilder** takes the request; **Target/arch** picks the opcode (CPU trap, not OS-thread IR).
- Number/ABI contract is the user’s (Linux x86_64 numbers ≠ Windows). Compiler does not memorize them.
- Runtime / bridge link is **optional** (`--no-runtime`, `--no-bridge`); object + baseline syslibs if needed for an exe.

Removed old model: [`syscalls.cpp`](src/syscalls.cpp) whitelist + emitting pthread bodies in IR.

Docs/rule: “declare = blind call · native syscall = CPU trap → kernel · `xl_*` = optional helper.”

---

## 2b) OS baseline link flags + embed

**No flags derived from syscall use** — fixed baseline per target OS (or from the platform table):

| Target | Example baseline (minimum; platform module completes) |
|--------|--------------------------------------------------------|
| linux | `-pthread` (or `-lpthread`), `-ldl -lm` when needed |
| macosx | `-pthread`; system frameworks in the platform table if needed |
| windows | CRT / Win32 import libs in the platform table (`kernel32`, etc. as needed) |

- Tables live under `platform/{linux,macosx,windows}`; the compiler only asks `host/resolve` for “this target’s syslib flag list” and appends it to the link line.
- **Embed:** when runtime/bridges are enabled, static-link extracted defaults (or overrides) into the **executable** (and into **static** libs when that mode links members as documented). **shared** is experimental. **object** stops before final image link per [LINKING.md](docs/LINKING.md).
- User may pass extra `.o`/`.a` (also static). Non-OS shared libs are not supported/encouraged by default.
- **`--no-runtime`:** must be set at that build — runtime not linked; **bridges still link** unless `--no-bridge`. Baseline OS flags still apply when producing an executable.
- **`--no-bridge`:** no default bridge link; raw compile — user supplies the rest.

---

## 3) Runtime build, version, embed, bootstrap

- Build runtime (frontend + OS bridges) to static archives / objects.
- Tarball root plaintext: `RUNTIME_VERSION`, `SUPPORTED_COMPILERS` (ranges/lines as already specified).
- **User compile:** resolve from (highest wins) `--runtime=` / `--bridge=` → else extract **embedded** defaults (unless `--no-runtime` / `--no-bridge`) → link per output mode + OS baseline flags when linking an exe.
- **Release embed bootstrap (required):**
  1. Stage-1: compile `xlang` **without** final xlang-runtime embed (bridges may already embed).
  2. Stage-1 `xlang` compiles `src/runtime/frontend` → runtime `.o` / archive.
  3. Stage-2: rebuild `xlang` from scratch with that runtime (and bridges) **embedded** for later extract-and-link.
- Local/dev may skip embed and use in-tree paths; release mode must perform the two-phase dance.
- Docs/rule: overrides, extract dir behavior, bootstrap comedy as official release steps.

---

## 4) Backbone: replace `paths` with `src/host/`

Remove [`src/paths.cpp`](src/paths.cpp) / [`include/xlang/paths.h`](include/xlang/paths.h).

Example layout:

```text
src/host/
  resolve.cpp      # target os/arch, override flags
  layout.cpp       # user/root/cache dirs (via platform API)
  runtime_pkg.cpp  # read RUNTIME_VERSION, compatibility, install path
  fetch.cpp        # GitHub release download (via platform HTTP/FS)
platform/
  linux/*.cpp
  macosx/*.cpp
  windows/*.cpp
```

Core (`lexer`, `parser`, `ast`, common codegen) **does not know paths/OS**; `host` + `platform/*` supply that.

---

## 5) Platform abstraction

- Host resolve: current platform/arch; `--target` / `--arch` / xmake `XLANG_TARGET_*` overrides; cross downloads that runtime.
- Platform backend API (e.g.): `home_dir`, `path_join`, `lib_dirs`, `download`, `extract_tarball`, `exe_suffix`, `run_process`.
- Raw `fork`/`unistd` in [`src/compiler.cpp`](src/compiler.cpp) / [`src/test_runner.cpp`](src/test_runner.cpp) → platform process API.

---

## 6) Codegen (Common IR Builder + Target emit)

```text
include/xlang/codegen/
  builder.h     # CommonIrBuilder — language → LLVM IR (OS-agnostic)
  target.h      # triple, DataLayout, TargetMachine → object
```

Flow:
1. `CommonIrBuilder`: functions, blocks, arithmetic, blind `declare`/`call`, **native-syscall request** (number+args).
2. `TargetMachine` / arch hook: object emit + native syscall opcode selection (`syscall` / `svc` / …).

Remove the string `.ll` path; use the LLVM C++ API (+ inline asm when needed).

---

## 7) Bridge layout (OS × arch)

```text
src/runtime/bridge/
  linux/{filesystem,net,thread,process,time,panic}.c(pp)
  macosx/...
  windows/...
```

- xmake builds only host or `-p` target OS bridges; arch defines (`XLANG_ARCH_X64`, etc.).
- Frontend [`src/runtime/frontend/`](src/runtime/frontend/) is OS-agnostic; each OS bridge implements the same declare names.

Thread: `xl_thread_start` / `xl_thread_join` / … on every OS — no pthread in language IR.

---

## 8) Simplify AST + lexer

- [`src/lexer.cpp`](src/lexer.cpp) / [`include/xlang/lexer.h`](include/xlang/lexer.h): single pass, keyword table, escapes; no platform paths.
- [`src/ast.cpp`](src/ast.cpp) / [`include/xlang/ast.h`](include/xlang/ast.h): tagged union + factories; trim needless helpers.
- May break; user fixes. Compile-tests not required.

---

## 9) CLI / xmake

- Flags: `--target`, `--arch`, `--runtime=`, `--bridge=`, `--no-runtime`, `--no-bridge`, `--runtime-version`, `--build=executable|static|shared|object` (default `executable`; shared = experimental), GitHub repo override.
- Default `xlang build foo.xlang`: executable + runtime + bridges + OS baseline syslibs.
- xmake: development (in-tree) vs release (two-phase embed bootstrap + publish).
- Targets: `xlang` (single CLI app) + runtime package target(s).

---

## Implementation order (single pass, no interrupt)

1. Scaffold `src/host/` + `platform/{linux,macosx,windows}` (including syslib flag tables); migrate/remove `paths`.
2. Blind declare→call; CPU-native syscall emit; OS baseline link.
3. Split bridges into OS folders; `xl_*` ABI; move thread/net I/O into bridges.
4. Runtime package + VERSION files; `--runtime=` / `--bridge=`; extract-to-temp/build link path.
5. Release two-phase bootstrap: stage-1 xlang → compile runtime → rebuild xlang with embed.
6. Codegen: CommonIrBuilder + TargetMachine; remove string `.ll`.
7. Cross/override + GitHub runtime fetch when not using embed/override.
8. Simplify AST/lexer.
9. Finalize xmake/docs/cursor rules (overrides + bootstrap comedy).
