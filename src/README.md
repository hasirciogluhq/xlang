# Source layout

Context folders — each concern owns its tree.

```
src/
  cli/                 # xlang CLI entry (`parse`, `check`, `layout`, `--explain`)
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
    lower.cpp          # body type checking + HIR lowering in one pass (plan 4.2-4.3)
  hir/
    hir.cpp            # typed HIR dump (`xlang check --hir`)
  util/
    source.cpp         # SourceManager / Span
    diagnostics.cpp    # code registry, rendering, allow/deny, log
test/compiler/         # unit tests (xlang-tests)
```

Headers: `include/xlang/{lang,sema,util}/*`.

There is no codegen yet; it returns with plan phase 4.7 (LLVM over the HIR).
