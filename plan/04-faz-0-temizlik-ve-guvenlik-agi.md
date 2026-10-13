## Faz 0 — Temizlik ve güvenlik ağı

- [ ] **0.1** `llvm::verifyModule()` — `generate.cpp`'de `release()` öncesi; hata → IR dump + hata.
- [x] **0.2** Eski test/example'a bağımlı hiçbir build adımı kalmasın; derleyici kendi küçük
  IR/parse testleriyle doğrulanır (`test/compiler/`, yeni).
- [x] **0.3** Ölü kod: eski dil yüzeyinin compiler'daki izleri (`declare syscall`, `@syscall`,
  `=>` ya da `:` dönüş tipi, `BigInt`/`Char`/`String` gibi kaldırılacak tipler) planlı olarak
  Faz 1–2'de silinir; eski sözdizimi için uyumluluk katmanı yazılmaz.

> **Durum notu:** 0.1 açık: `verifyModule` LLVM IR üretimine bağlıdır; eski codegen hattı (`src/codegen`,
> `src/compiler`, `src/host`, `src/platform`, ilgili `include/` başlıkları, `examples/`, `test/xlang/`) **silindi**,
> yeni codegen 4.7'de yazılırken `verifyModule` ilk işi olarak eklenir. 0.2 tamam: `test/compiler/` (`xlang-tests`
> hedefi: lexer, parser, diagnostics, preprocess, macro, sema, layout, conversion). 0.3 tamam: eski dil yüzeyi
> izleri eski hatla birlikte silindi.
