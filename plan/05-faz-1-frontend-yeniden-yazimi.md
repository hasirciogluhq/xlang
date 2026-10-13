## Faz 1 — Frontend yeniden yazımı (yeni sözdizimi)

- [ ] **1.1** Lexer: `->`, `#if/#elif/#else`, `@attr`, yeni tip anahtar sözcükleri (`i8..i64`, `ui8..ui64`,
  `isize`, `usize`, `f32`, `f64`), `class`, `interface`, `impl`/`:` kalıtım sözdizimi.
- [ ] **1.2** Parser: `fn ... -> T`, öznitelikler (`@repr(c)`, `@align(N)`, `@packed`), `class`/`interface`
  bildirimleri, `declare fn ... -> T`, fonksiyon pointer tipi `fn(A, B) -> R`, dizi tipi `[T; N]`,
  dizi literal'i `[a, b]` / `[v; N]`, indeksleme `a[i]`, `enum Ad: T { ... }`, `new T { ... }` /
  `new(a) T { ... }` ifadesi, `register[(rol)]` öneki.
- [ ] **1.2b** Import biçimleri: `import "m" as a`, `import * from "m"`, `import * as a from "m"`,
  `from "m" import x [as y], ...` ve
  `import x [as y], ... from "m"`; ikincisi birincisiyle aynı `ImportDecl` düğümüne iner.
  Parser, `import` sonrasında string mi tanımlayıcı mı geldiğine bakarak ayırır.
- [ ] **1.2c** `type Ad = Tip` (+ `export type`), `define N = ifade`, `define f(p: tür, ...) { gövde }`,
  macro çağrısı `ad!(...)`. Macro gövdesi ham token ağacı olarak saklanır (parse genişleme anında,
  parametre türüne göre).
- [ ] **1.2d** `declare` genelleştirme: tek önek, parser her bildirimi "gövdesiz/dış" bayrağıyla ayrıştırır
  (`declare fn/global/struct/class/interface/type`). Global için ayrı kural yok; opak `declare struct S`.
  `global ad: T [= değer]` normal global bildirimi.
- [ ] **1.2e** `export * from`, `export * as ns from`, `export { ... } from`, `export * from ... except ...`;
  `#if` içinde de kullanılabilir.
- [ ] **1.3** AST'yi yeniden tasarla: tip düğümü `Type` ağaç yapısı (`TypeKind::Int{bits, signed}`,
  `Float{bits}`, `Pointer`, `FnPtr`, `Array{elem, N}`, `Enum{underlying}`, `Struct`, `Class`, `Interface`). Eski
  `Int32/Int64/BigInt` ayrımı gider.
- [ ] **1.4** Diagnostics altyapısı (`include/xlang/util/diagnostics.h`): `Diagnostic{code, severity, span,
  message, detail, reason, fix, notes}`, snippet'li çıktı, birden fazla hata toplama. Her uyarı
  `ne + detay + neden + nasıl düzeltilir` taşır. Kodlar (`W0201`...) bir kayıt tablosunda; `--allow=KOD`,
  `--deny=KOD`, `-Wno-KOD`, `--warn-all`, `--allow-all`, `--explain KOD`, `@allow(KOD)` (bildirim/dosya kapsamı).
  Compiler kısıtlamaz: `declare` ile bağlanan dış şeyler (xl/C++/Rust/`.o`/statik) yalnızca uyarı üretir.
  - **Nokta atışı gösterim:** tanı, hatalı satırın sonuna `<======= yorum` ile işaretlenir; olayın birden
    fazla yeri varsa (ör. çakışmanın iki tarafı) her biri kendi satırında kendi yorumuyla gösterilir; aynı
    satırda birden fazla nokta varsa altına `^^^` ile tam token vurgulanır. Çok satırlı span'lerde başlangıç
    ve bitiş satırı işaretlenir. Konum bilgisi (`Span`) dosya + satır + sütun aralığı taşır.
  - **Bağlam satırları:** işaretli noktadan yukarı/aşağı bağlam önem derecesine göre: hata varsayılanı
    −10/+10, uyarı −2/+2; `--context-errors=N`, `--context-warnings=N`, `--context=N`. Satır numarası
    sütunu en büyük numaraya göre sağa hizalı; yakın işaretler tek blokta birleşir (örtüşen bağlam
    tekrarlanmaz), uzak işaretler arası `...`; dosya sınırında kırpma; sekme açma ve uzun satır kısaltma
    (işaretli sütun korunur).
  - **Log:** `--diag-log=<dosya>`, `--diag-format=text|json`, `--diag-log-level=all|warn|error`; susturulan
    tanılar log'da "susturuldu" işaretiyle tutulur.

**Kabul:** yeni sözdizimli küçük programlar parse edilir ve `xlang parse` ile AST özeti basılır.
