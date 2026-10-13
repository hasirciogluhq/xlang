# xlang Compiler Geliştirme Planı

Hedef: mevcut "parse → AST → (codegen içinde tip çözümleme) → LLVM IR" hattını
profesyonel bir derleyici mimarisine taşımak.

Hedef hat:

```
Lexer → Parser → AST → Module Resolve → Sema → HIR → LLVM IR → Optimize → Object → Link
```

## Kararlar

| Konu | Karar | Gerekçe |
|------|-------|---------|
| Syscall | Compiler builtin `syscall(nr, args...)` (yapıldı) | Compiler hangi syscall olduğunu bilmez, sadece trap üretir |
| HIR | **Eklenecek** | Tip çözümlemesi codegen'den ayrılmalı; codegen sadece tipli ağacı görmeli |
| LIR | **Eklenmeyecek** | LLVM IR zaten LIR. Araya ikinci bir düşük seviye IR koymak çeviri maliyeti ve bakım yükü getirir, kazanç sağlamaz (aşağıya bak) |
| Derleme modeli | Şimdilik whole-program (tek modül) | Doğru model; incremental en sona |
| Pass manager | New PassManager (`PassBuilder`) | Legacy PM deprecated |

### LIR neden yok?

LIR (düşük seviye, makineye yakın IR) ancak şu durumlarda mantıklıdır:

- LLVM dışında kendi backend'imizi yazıyorsak,
- Birden fazla backend (LLVM + Cranelift/WASM/kendi asm) hedefliyorsak,
- LLVM'in yapamadığı dile özgü optimizasyonlar (ownership/borrow, escape analysis) için CFG tabanlı kendi IR'ımız gerekiyorsa.

Bunların hiçbiri şu an yok. LLVM IR SSA, CFG, hedef bağımsız ve optimize edilebilir: LIR'ın tüm işini yapıyor.

**Yeniden değerlendirme tetikleyicileri:** ikinci bir backend, ya da dile özgü
CFG analizi ihtiyacı. O zaman HIR ile LLVM arasına **MIR** (CFG tabanlı, Rust MIR benzeri)
eklenir, "LIR" adıyla değil.

---

## Faz 0 — Güvenlik ağı (küçük, hemen)

Her fazdan önce bunlar olmalı ki regresyon yakalansın.

- [ ] **0.1** `llvm::verifyModule()` ekle: `src/codegen/generate.cpp`, `release()` öncesi.
  Hata varsa `XlangError` ile IR dump'ını yazdır. Release build'de de açık kalsın (ucuz).
- [ ] **0.2** Syscall/IR smoke testi: `test/` altına `syscall(nr, ...)` için 0–6 argümanlı IR kontrol testi
  (`--emit-ir` çıktısında `asm sideeffect` kontrolü).
- [ ] **0.3** Mevcut `test/xlang/*.test.xlang` suitini CI'da / `xlang test` ile her faz sonunda çalıştır.

**Kabul:** hatalı IR üretilince compiler crash yerine okunabilir hata verir.

## Faz 1 — Codegen düzeltmeleri (küçük, izole)

- [ ] **1.1 Alloca'ları entry bloğuna taşı.**
  - `src/codegen/builder.cpp` `emitAlloca`: ayrı bir `IRBuilder<>` ile
    `fn->getEntryBlock().getFirstInsertionPt()` konumuna ekle.
  - `src/codegen/functions.cpp` `allocLocal` bu yolu kullanır.
  - **Kabul:** döngü içindeki `local` için IR'da `alloca` yalnızca `entry:` bloğunda görünür.
- [ ] **1.2 Optimizasyon pipeline'ı.**
  - `src/codegen/target.cpp`: `emitObject` içinde `PassBuilder` ile
    `buildPerModuleDefaultPipeline(level)` çalıştır, sonra codegen.
  - `CodegenOptions`'a `opt_level` (O0/O1/O2/O3) ekle, CLI'ya `-O0..-O3` bayrağı
    (`src/cli`). Varsayılan: `O2`, `--emit-ir`'da `O0`.
  - `TargetMachine` oluştururken `CodeGenOptLevel` bağla; CPU `generic` kalsın,
    `--cpu native` opsiyonel bayrak olsun.
  - **Kabul:** `local x = 1 + 2` IR'da sabite katlanır, döngü local'leri `phi`'ye promote olur.
- [ ] **1.3 Syscall hedef OS ayrımı.**
  - `src/codegen/target.cpp` `nativeSyscallAsm`: arch yanında OS'a (`llvm::Triple::isOSDarwin`) bak.
  - Darwin x86_64: numaraya `0x2000000` ekle. Darwin aarch64: numara `x16`, trap `svc #0x80`.
  - Desteklenmeyen OS/arch kombinasyonu için açık hata.
  - **Kabul:** macOS ve Linux için ayrı IR üretimi testi.
- [ ] **1.4 Pointer ↔ integer dönüşümlerini azalt.**
  `asI64`/`inttoptr` kullanımlarını sadece syscall sınırında bırak (HIR sonrası Faz 3'te kalıcı çözülür).

## Faz 2 — Frontend ayrıştırma: Diagnostics + Sema

- [ ] **2.1 Diagnostics altyapısı.**
  - Yeni: `include/xlang/util/diagnostics.h`, `src/util/diagnostics.cpp`.
  - `Diagnostic { severity, span, message, notes }`, kaynak snippet'i ve `dosya:satır:sütun` basımı.
  - `XlangError` fırlatan yerler kademeli olarak `DiagnosticEngine`'e taşınır;
    tek hatada durmak yerine mümkün olduğunda birden fazla hata toplanır.
- [ ] **2.2 Sembol tablosu ve scope.**
  - Yeni: `include/xlang/sema/scope.h`, `src/sema/scope.cpp`.
  - Modül, fonksiyon, blok scope'ları; nitelikli isim (`alias.name`) çözümü burada.
- [ ] **2.3 Sema geçişi.**
  - Yeni: `include/xlang/sema/sema.h`, `src/sema/sema.cpp`.
  - Yapacakları: isim çözümleme, tip çıkarımı/kontrolü, overload seçimi
    (`resolve.cpp` içindeki mantık buraya taşınır), `external`/`declare` doğrulama,
    `syscall(...)` için argüman sayısı (1–7) ve tamsayı/pointer tipi kontrolü.
  - Çıktı: hata listesi + HIR (Faz 3).
- [ ] **2.4 Modül yüklemeyi sadeleştir.**
  - `src/compiler/module.cpp`: AST klonlama ve elle isim önekleme yerine modül başına
    ayrı `Program` + sembol tablosu tutulur; birleştirme sema aşamasında scope bağlama olur.
  - Mangling tek yerde (`src/lang/types` veya `src/sema/mangle.cpp`).
  - **Kabul:** mevcut import testleri (`from`, alias, seçili import) aynen geçer.

## Faz 3 — HIR

HIR = sema sonrası, **tipleri ve sembolleri çözülmüş**, sugar'ı açılmış ağaç.
Codegen artık `Program`/AST'yi değil HIR'ı tüketir.

- [ ] **3.1 HIR tanımı.**
  - Yeni: `include/xlang/hir/hir.h`.
  - Her ifade düğümü açık `Type` taşır (çıkarım sonrası), her referans çözülmüş sembol id'sidir.
  - Örtük dönüşümler (i32→i64 widening, bool→i8, pointer cast) **açık `Cast` düğümü** olarak yazılır.
    Böylece codegen `asI64` tarzı tahminler yapmaz.
  - `syscall` düğümü: `Syscall { nr, args }` — tüm argümanlar sema'da i64'e açıkça cast edilmiş.
  - `go`/`spawn`, `print` gibi sugar'lar düşürülmüş (lowered) halde.
- [ ] **3.2 AST → HIR lowering.**
  - Yeni: `src/hir/lower.cpp`. Sema çıktısıyla birlikte tek geçişte üretilir.
- [ ] **3.3 HIR printer/dump.**
  - `xlang parse --emit-hir` bayrağı (test ve debug için).
- [ ] **3.4 Codegen'i HIR'a taşı.**
  - `src/codegen/expr.cpp`, `stmt.cpp`, `functions.cpp`, `structs.cpp`, `strings.cpp`, `globals.cpp`
    HIR düğümleri üzerinden çalışır. `resolve.cpp` codegen'den kalkar (sema'ya taşındı).
  - Bir dosya bir dosya taşı; her adımda test suiti yeşil olmalı. Geçişte AST ve HIR yolları
    bir süre yan yana yaşayabilir.
  - **Kabul:** codegen içinde tip çıkarımı/overload çözümü kalmaz; `XlangError("...must be integer types")`
    benzeri tip hataları sema'da diagnostic olarak çıkar.

## Faz 4 — Araçlar ve kalite

- [ ] **4.1 Debug info.** `llvm::DIBuilder` ile `DICompileUnit`, `DISubprogram`, satır konumları
  (`Span` zaten var). `-g` bayrağı.
- [ ] **4.2 Hedef ve ABI netleştirme.** `external`/`declare` fonksiyonlar için parametre/dönüş
  ABI kontrolü; struct by-value geçişi için hedefe özel kural veya açık hata.
- [ ] **4.3 Test altyapısı.**
  - Sema için hata testleri (beklenen diagnostic mesajı/konumu).
  - IR golden testleri (küçük programlar, `O0` IR snapshot).
  - Cross-target IR testi (x86_64-linux, aarch64-linux, aarch64-darwin; sadece IR üretimi).
- [ ] **4.4 Dokümantasyon.** `docs/LANGUAGE.md`, `src/README.md`, `examples/welcome.html`
  içindeki `declare syscall` / `@syscall` ifadelerini `syscall(nr, ...)` ile güncelle;
  yeni pipeline'ı `src/README.md`'ye yaz.

## Faz 5 — Ölçekleme (ihtiyaç doğduğunda)

- [ ] **5.1 Modül başına derleme.** Her modül kendi LLVM modülü/`.o`; modül arayüz dosyası
  (export edilen semboller + tipler) üretimi; bağımlılık grafiği.
- [ ] **5.2 Incremental/cache.** İçerik hash'ine dayalı `.o` veya bitcode cache.
- [ ] **5.3 (Koşullu) MIR.** Yalnızca ikinci backend veya dile özgü CFG analizi gerekirse
  HIR ile LLVM arasına CFG tabanlı MIR ekle. Bkz. "LIR neden yok?".

---

## Sıra ve bağımlılıklar

```
Faz 0 ─► Faz 1 ─► Faz 2 ─► Faz 3 ─► Faz 4
 (güvenlik)  (hızlı     (sema)    (HIR)    (kalite)
             kazançlar)                      │
                                             └─► Faz 5 (ölçek)
```

- Faz 0 ve 1 birbirinden bağımsız küçük PR'lara bölünebilir (1.1, 1.2, 1.3 ayrı).
- Faz 2 → 3 sıralı: HIR, sema çıktısı olmadan anlamlı değil.
- Faz 3.4 en riskli adım; dosya dosya ilerle, her adımda `xlang test`.

## Önerilen PR bölünmesi

1. `verifyModule` + syscall smoke testleri (0.1–0.2)
2. Alloca entry-block (1.1)
3. PassBuilder + `-O` bayrağı (1.2)
4. Darwin syscall konvansiyonu (1.3)
5. Diagnostics (2.1)
6. Scope + Sema iskeleti (2.2–2.3)
7. Module loader sadeleştirme (2.4)
8. HIR tanımı + lowering + dump (3.1–3.3)
9. Codegen'in HIR'a geçişi (3.4, birden fazla PR)
10. Debug info, testler, dokümanlar (Faz 4)
