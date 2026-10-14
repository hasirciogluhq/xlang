# Source layout

Context folders — each concern owns its tree.

```
src/
  cli/                 # xlang CLI entry (`parse`, `check`, `build`, `layout`, `run`, `test`, `--explain`)
    test_runner.cpp    # `xlang run` and `xlang test`: file/glob/regex selection, build + run per file, colored report
  lang/
    lexer/             # tokens; `#if` ... directives are lexed here
    preprocess/        # conditional compilation on the token stream
    macro/             # `define f(x: expr) { }` / `f!(...)` token-level expansion
    parser/            # recursive descent, error recovery
    ast/               # AST dump (`xlang parse`)
    types/             # Type tree + primitive names
    config.cpp         # BuildConfig, target triples, -D values
    frontend.cpp       # lex -> preprocess -> macros -> parse
  sema/
    sema.cpp           # names, aliases, enums, const eval, layouts, staticAssert
    layout.cpp         # layout record / xl.layout history
    conversion.cpp     # implicit conversion rules (E0601 / W0601)
    scope.cpp          # ScopeStack: module -> function -> block scopes (plan 4.1)
    compilation.cpp    # module loading, exports/imports, registrations, whole-program driver (4.6, 4.12)
    lower.cpp          # body type checking + HIR lowering over all modules (4.2-4.5, 4.8-4.12)
  hir/
    hir.cpp            # typed HIR dump (`xlang check --hir`)
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
test/e2e/              # end-to-end programs + run.sh
std/                   # the standard library, plain xl (plan phase 7)
```

Headers: `include/xlang/{lang,sema,hir,codegen,util}/*`.

