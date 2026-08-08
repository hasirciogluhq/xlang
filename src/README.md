# Source layout

Context folders — each concern owns its tree. Prefer adding a new file under the right folder over growing a root blob.

```
src/
  cli/                 # xlang CLI entry (main)
  lang/
    ast/ lexer/ parser/ types/
  codegen/
    generate.cpp       # Codegen::generate entry + CodegenResult
    builder.cpp        # CommonIrBuilder (LLVM IR API)
    target.cpp         # TargetMachine / object emit / native syscall asm
    lower_types.cpp    # llvmType, coerce, sizes
    prelude.cpp        # declares / prelude
    strings.cpp arrays.cpp structs.cpp globals.cpp
    functions.cpp      # fn declare/define, locals
    stmt.cpp           # statements (if/while/assign/…)
    expr.cpp           # expressions + @syscall
    print.cpp          # print / spawn helpers
    resolve.cpp        # overload / method / var resolve
    detail/helpers.cpp # matching + use-analysis helpers
  compiler/            # compile, link, module load, runtime package
    test/              # test runner
  host/                # layout, resolve, fetch, embed, runtime_pkg
  platform/            # OS backends (linux|macosx|windows)
  util/
  runtime/             # frontend .xlang + bridge C (not in compiler TU list)
```

Headers mirror under `include/xlang/{lang,codegen,compiler,host,util}/` with thin facades at the old `include/xlang/*.h` paths.
