## 1. Hedef hat

```
Source ─► Lexer ─► Preprocess (#if/#elif/#else/#endif, token seviyesi) ─► Parser ─► AST ─► Macro expand ─► Module resolve
       ─► Sema (tip, isim, layout) ─► HIR ─► LLVM IR ─► Optimize ─► Object ─► (--emit'e göre) Link / Arşiv
```

Compiler her çıktıyı kendisi üretir; `--emit` hangi aşamada duracağını seçer. Varsayılan `exe`'dir ve
hat sonuna kadar çalışır. `ir`/`bc`/`obj`/`header` istenirse o noktada durur, üretir ve bırakır. `exe`/`shared`
için LLVM'in linker'ını (ya da hedefin linker'ını), `static` için arşivleyiciyi gerekirse çağırarak işi bitirir.
Compiler kendi çıktısının dışında bir şey bağlamaz (libc, dış kütüphane yok).
