## 1. Hedef hat

```
Source ─► Lexer ─► Preprocess (#if/#elif/#else/#endif, token seviyesi) ─► Parser ─► AST ─► Macro expand ─► Module resolve
       ─► Sema (tip, isim, layout) ─► HIR ─► LLVM IR ─► Optimize ─► Object ─► Link
```
