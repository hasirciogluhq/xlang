# Source layout

Context folders — each concern owns its tree.

```
src/
  cli/                 # xlang CLI entry (`parse`, `check`, `build`, `layout`, `run`, `test`, `package`, `self-update`, `completions`, `--explain`, `--version`, `--color`)
    process.cpp        # child processes (`xlang::host::run`: `fork`/`poll` or `CreateProcessW`), shared by the CLI and by `util/program.cpp`/`pkg/git.cpp`, so it is built into `xlang-frontend`
    app/
      main.cpp         # CLI11 command tree, `parse`/`check`/`build`/`layout`/`run` wiring and exit codes
      style.h          # ANSI `Palette` and `useColor` (`--color`, `NO_COLOR`, `XLANG_COLOR`) shared by the CLI
      version.h        # `kVersion`, the `XLANG_VERSION` define from xmake.lua
    tests/
      runner.cpp       # `xlang run` and `xlang test`: file/glob/regex selection, build + run per file, colored report
    update/
      update.cpp       # `xlang self-update` (aliases `update`, `upgrade`): latest release, checksum, in-place replace
    completions/
      completions.cpp  # `xlang completions bash|zsh|fish|install`: scripts generated from the CLI11 command tree; `install` sets up the shells in use
    package/
      package.cpp      # `xlang package install|update|remove|list|clean` and `attachProject` (xlang.toml -> packages, stdlib source, native files for a build)
  lang/
    lexer/             # tokens; `#if` ... directives are lexed here
    preprocess/        # conditional compilation on the token stream
    macro/             # `define f(x: expr) { }` / `f!(...)` token-level expansion
    parser/            # recursive descent, error recovery; a payload enum is split into struct/union/tag items here
    ast/               # AST dump (`xlang parse`)
    types/             # Type tree + primitive names
    config.cpp         # BuildConfig, target triples, -D values
    frontend.cpp       # lex -> preprocess -> macros -> parse
  sema/
    sema/              # per-module semantic services (plan phases 2 and 3.6/3.8); `sema.hpp` is the internal header
      const_value.cpp  #   `ConstValue` and its text form
      symbols.cpp      #   name table (`collect`, E0401/E0405), diagnostics with instantiation notes, link names, `fn alias`
      instances.cpp    #   generic instances: `instantiate`, method instances, the pending queues
      types.cpp        #   `resolve`: Named -> Struct/Class/Interface/Enum, alias expansion (E0704/E0724)
      module_scope.cpp #   import bindings, qualified lookup, name expression -> entity, expression -> type
      const_eval.cpp   #   compile-time evaluation: literals, defines, enum members, operators, casts, sizeOf/alignOf/offsetOf
      enums.cpp        #   enum member values and E0705 checks
      layout.cpp       #   sizes, `str`, arrays, aggregate layout (scheme v1, @repr(c), @packed, @align, records)
      checks.cpp       #   whole-module checks and `analyze`
    layout.cpp         # layout record / xl.layout history
    conversion.cpp     # implicit conversion rules (E0601 / W0601)
    scope.cpp          # ScopeStack: module -> function -> block scopes (plan 4.1)
    compilation/       # whole-program driver (4.6, 4.12); `compilation.hpp` is the internal header
      locate.cpp       #   import spec -> file, macros seeding, module loading and `ModuleInfo` registration
      exports.cpp      #   per-module export set (`export`, `export ... from`, `except`, `as`)
      imports.cpp      #   per-module import bindings and duplicate/conflict diagnostics (E0401/W0401)
      registered.cpp   #   `register(role)` names across the compilation, user over stdlib, E0502
      driver.cpp       #   lifecycle, phase order (`finish`/`analyzeAll`/`lowerAll`), `@init` order, W0301
    lower/             # lowering pass (4.2-4.5, 4.8-4.12); `lower.hpp` is the internal header
      items.cpp        #   pass entry points and cross-module orchestration; items: module scope, registrations, vtables, impls, declarations, payload enum variants (E0749-E0751, E0757-E0759)
      stmt.cpp         #   statements (control flow, `defer`, `try`) and ownership (drop glue, drop flags, moves; E0758/E0759 for payload enums that need drop)
      expr.cpp         #   expressions, calls, overload resolution and conversions
    variadic.cpp       # `..args` packs: per-count instances, args.len folding, unrolled for, pack checks (E0734-E0738)
  pkg/
    toml.cpp           # TOML subset parser (xlang.toml, xlang.lock)
    semver.cpp         # release tags: `v?X.Y.Z`, pre-release, `-test` tags, latest
    manifest.cpp       # xlang.toml model + validation, in-place text edits that keep comments
    git.cpp            # `git` as a child process: ls-remote tags, clone a tag into the cache
    project.cpp        # resolver: lock + cache (offline) or fetch (install), transitive deps, conflicts, native inputs, xlang.lock
  hir/
    hir.cpp            # typed HIR dump (`xlang check --hir`)
    prune.cpp          # reachability pruning: drops functions and globals no root reaches (plan 24)
  codegen/
    codegen.cpp        # HIR -> LLVM IR driver: context, module, target machine, data layout (4.7)
    llvm/              # the pass itself, split by topic; `gen.hpp` is the internal header
      gen.hpp          #   `Gen` (the per-module emitter) and the helpers its parts share
      types.cpp        #   SSA type of a scalar, in-memory type, size/align, function types
      memory.cpp       #   allocas, scalar load/store, aggregate copy/zero, field and byte addresses, panic calls
      unwind.cpp       #   landing pads and `try` (5.3)
      declarations.cpp #   globals, functions, vtables/itables, constant initializers
      functions.cpp    #   prologue/epilogue of a function and statement lowering
      expressions.cpp  #   addresses, memory results, casts, binary operators, calls, `new`, `syscall`
      atomics_asm.cpp  #   `__builtin_atomic*`/`__builtin_fence` and `__builtin_asm`
      scalar.cpp       #   scalar expression dispatch and constant expressions
      entry.cpp        #   `_xl_initAll`/`_xl_finiAll`, library hooks, `_start`/`mainCRTStartup` stubs, `Gen::run`
    emit.cpp           # target machine, pass pipeline, obj/ir/bc writers (5.2, 5.6)
    header.cpp         # `--emit=header` (5.7)
  driver/
    link.cpp           # linker driver + in-process archiver (5.8, 5.9)
  util/
    source.cpp         # SourceManager / Span
    diagnostics.cpp    # code registry, rendering, allow/deny, log
    program.cpp        # running program's image path, `programOutput` (captured stdout), `processId`
test/compiler/         # unit tests (xlang-tests)
test/e2e/              # end-to-end programs + run.sh (+ lib.sh: filter and job pool; packages.sh: the package system)
std/                   # the standard library, plain xl (plan phase 7), nested by area, each area with index.xl
  runtime/             #   panic, unwind (try/catch walker, register(unwind)), context (stack switch)
  sys/                 #   the per-OS raw layer (linux, macos, windows, sock); OS setup hidden behind sockUp-style functions
  mem/                 #   mem (Allocator, Heap, currentAllocator), memory (memcpy family)
  str/                 #   str, string (String), fmt (placeholder engine)
  collections/         #   vec (Vec<T>)
  io/                  #   io (print family), fs (whole-file helpers)
  math/                #   math, bignum (fixed-width integers for RSA and ECDSA)
  encoding/            #   json, pem
  cpu/, gpu/           #   CPU helpers; GPU access
  thread/              #   thread, sync, sched (cooperative scheduler)
  testing/             #   testing (framework), assert
  crypto/              #   hash/ (sha256, sha512, digest, hmac), cipher/ (aesgcm, chacha20poly1305), kex/ (x25519), sig/ (rsa, ecdsa), x509/ (x509, privkey)
  net/                 #   dns (planned: IPv4/IPv6 address types, sockets, TCP/UDP)
  tls/                 #   tls (TLS 1.3), truststore (root store and chain checks)
  http/                #   HTTP/1.1 and HTTPS server and client (util, url, stream, fetch, context, router, middleware, server)
```

Headers: `include/xlang/{lang,sema,hir,codegen,util}/*`.

