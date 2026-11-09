# Source layout

Context folders — each concern owns its tree.

```
src/
  cli/                 # xlang CLI entry (`parse`, `check`, `build`, `layout`, `run`, `test`, `package`, `self-update`, `completions`, `--explain`, `--version`, `--color`)
    test_runner.cpp    # `xlang run` and `xlang test`: file/glob/regex selection, build + run per file, colored report
    update.cpp         # `xlang self-update` (aliases `update`, `upgrade`): latest release, checksum, in-place replace
    completions.cpp    # `xlang completions bash|zsh|fish|install`: scripts generated from the CLI11 command tree; `install` sets up the shells in use
    package.cpp        # `xlang package install|update|remove|list|clean` and `attachProject` (xlang.toml -> packages, stdlib source, native files for a build)
    style.h            # ANSI `Palette` and `useColor` (`--color`, `NO_COLOR`, `XLANG_COLOR`) shared by the CLI
    version.h          # `kVersion`, the `XLANG_VERSION` define from xmake.lua
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
    sema.cpp           # names, aliases, enums, const eval, layouts, staticAssert, generic instances and method instances
    layout.cpp         # layout record / xl.layout history
    conversion.cpp     # implicit conversion rules (E0601 / W0601)
    scope.cpp          # ScopeStack: module -> function -> block scopes (plan 4.1)
    compilation.cpp    # module loading, exports/imports, registrations, whole-program driver (4.6, 4.12)
    lower.cpp          # body type checking + HIR lowering over all modules (4.2-4.5, 4.8-4.12); payload enum variants and `switch` (E0749-E0751)
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
    codegen.cpp        # HIR -> LLVM IR (4.7, 5.1, 5.3, 5.4, 6)
    emit.cpp           # target machine, pass pipeline, obj/ir/bc writers (5.2, 5.6)
    header.cpp         # `--emit=header` (5.7)
  driver/
    link.cpp           # linker driver + in-process archiver (5.8, 5.9)
  util/
    source.cpp         # SourceManager / Span
    diagnostics.cpp    # code registry, rendering, allow/deny, log
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

