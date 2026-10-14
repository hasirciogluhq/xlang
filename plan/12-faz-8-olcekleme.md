## Faz 8 — Ölçekleme (ihtiyaç doğunca)

- [x] **8.1** Modül başına derleme: modül başına LLVM modülü/`.o`, arayüz dosyası, bağımlılık grafiği.
- [x] **8.2** İçerik hash'li incremental/cache.
- [ ] **8.3** (Koşullu) MIR: yalnızca ikinci backend / dile özgü CFG analizi gerekirse.

> **Durum notu (8):** `xlang build --incremental`: modül başına LLVM modülü/`.o`; vtable/itable'lar her
> modülde zayıf (`linkonce_odr`) tanımdır, tüm işlevler dış bağlantılıdır. Önbellek anahtarı: derleyici
> sürümü + yapılandırma (hedef, `-O`, `-g`, cpu, mangle, safety, `-D`, std özellikleri, emit) + modül
> kaynağı + bağımlılıklarının hash'leri + `register`'lı modüllerin hash'i. Arayüz dosyası yazılmaz: sema her
> derlemede tüm modülleri çözümler, yalnızca LLVM/backend işi önbelleklenir (8.3 MIR: gerek duyulmadı).
