## PR bölünmesi

1. `verifyModule` + yeni test iskeleti (0)
2. Lexer/parser yeni sözdizimi + AST/Type yeniden tasarımı (1.1–1.3)
3. Diagnostics (1.4)
4. Tip modeli + işaretli/işaretsiz lowering (2.1–2.2)
5. Layout motoru + `@repr(c)` + `size_of/offset_of/static_assert` + clang karşılaştırma testi (2.3–2.5);
   enum (2.6)
6. `#if` + yerleşik tanımlar (3.1–3.3, 3.5)
7. CLI bayrakları `-D/-U/--target/-O/-g/--emit/--print-cfg` + `define` sabitleri (3.4, 3.6)
7b. Macro genişletme + `type` takma adı (3.7–3.8)
8. Sema + scope + modül yükleme (4.1–4.2, 4.6)
9. HIR + codegen taşıma (4.3, 4.7)
10. Class (4.4), Interface (4.5)
10b. Görünürlük + mangling + `ref T` + `new` + `register` (4.8–4.12)
11. Alloca/PassBuilder/Darwin syscall/debug info (5.1–5.5)
11b. Çıktı biçimleri + header + static/shared (5.6–5.8)
12. Atomic + fnptr + asm (6)
13. Stdlib (7.1–7.6, 7.2b), ardından compiler'dan kaldırma (7.7)
