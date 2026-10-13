## Faz 0 — Temizlik ve güvenlik ağı

- [ ] **0.1** `llvm::verifyModule()` — `generate.cpp`'de `release()` öncesi; hata → IR dump + hata.
- [ ] **0.2** Eski test/example'a bağımlı hiçbir build adımı kalmasın; derleyici kendi küçük
  IR/parse testleriyle doğrulanır (`test/compiler/`, yeni).
- [ ] **0.3** Ölü kod: eski dil yüzeyinin compiler'daki izleri (`declare syscall`, `@syscall`,
  `=>` ya da `:` dönüş tipi, `BigInt`/`Char`/`String` gibi kaldırılacak tipler) planlı olarak
  Faz 1–2'de silinir; eski sözdizimi için uyumluluk katmanı yazılmaz.
