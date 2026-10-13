## Faz 1 — Frontend yeniden yazımı (yeni sözdizimi)

- [x] **1.1** Lexer: `->`, `#if/#elif/#else`, `@attr`, yeni tip anahtar sözcükleri (`i8..i64`, `ui8..ui64`,
  `isize`, `usize`, `f32`, `f64`), `class`, `interface`, `impl`/`:` kalıtım sözdizimi, `unsafe`.
- [x] **1.2** Parser: `fn ... -> T`, öznitelikler (`@repr(c)`, `@align(N)`, `@packed`), `class`/`interface`
  bildirimleri, `declare fn ... -> T`, fonksiyon pointer tipi `fn(A, B) -> R`, dizi tipi `[T; N]`,
  dizi literal'i `[a, b]` / `[v; N]`, indeksleme `a[i]`, `enum Ad: T { ... }`, `new T { ... }` /
  `new(a) T { ... }` ifadesi, `register[(rol)]` öneki, `unsafe { ... }` statement bloğu, statement/blok
  üzerinde öznitelik (`@allow(KOD) local ...`).
- [x] **1.2b** Import biçimleri: `import "m" as a`, `import * from "m"`, `import * as a from "m"`,
  `from "m" import x [as y], ...` ve
  `import x [as y], ... from "m"`; ikincisi birincisiyle aynı `ImportDecl` düğümüne iner.
  Parser, `import` sonrasında string mi tanımlayıcı mı geldiğine bakarak ayırır.
- [x] **1.2c** `type Ad = Tip` (+ `export type`), `define N = ifade`, `define f(p: tür, ...) { gövde }`,
  macro çağrısı `ad!(...)`. Macro gövdesi ham token ağacı olarak saklanır (parse genişleme anında,
  parametre türüne göre).
- [x] **1.2d** `declare` genelleştirme: tek önek, parser her bildirimi "gövdesiz/dış" bayrağıyla ayrıştırır
  (`declare fn/global/struct/class/interface/type`). Global için ayrı kural yok; opak `declare struct S`.
  `global ad: T [= değer]` normal global bildirimi.
- [x] **1.2e** `export * from`, `export * as ns from`, `export { ... } from`, `export * from ... except ...`;
  `#if` içinde de kullanılabilir.
- [x] **1.3** AST'yi yeniden tasarla: tip düğümü `Type` ağaç yapısı (`TypeKind::Int{bits, signed}`,
  `Float{bits}`, `Pointer`, `FnPtr`, `Array{elem, N}`, `Enum{underlying}`, `Struct`, `Class`, `Interface`). Eski
  `Int32/Int64/BigInt` ayrımı gider.
- [x] **1.4** Diagnostics altyapısı (`include/xlang/util/diagnostics.h`): `Diagnostic{code, severity, span,
  message, detail, reason, fix, notes}`, snippet'li çıktı, birden fazla hata toplama. Her uyarı
  `ne + detay + neden + nasıl düzeltilir` taşır. Kodlar (`W0201`...) bir kayıt tablosunda; `--allow=KOD`,
  `--deny=KOD`, `-Wno-KOD`, `--warn-all`, `--allow-all`, `--explain KOD`, `@allow(KOD)` (statement/blok/bildirim/dosya kapsamı).
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

**Kabul:** yeni sözdizimli küçük programlar parse edilir ve `xlang parse` ile AST özeti basılır. (**Karşılandı**:
`xlang parse f.xl` öğe özetini, `--ast` tam ağacı basar; birim testleri `test/compiler/`, `xmake build xlang-tests
&& xmake run xlang-tests`.)

### Faz 1 uygulama notları (yapılanlar ve sapmalar)

- **Dosya düzeni:** `include/xlang/lang/{lexer,ast,types,parser}.h`, `include/xlang/util/{source,diagnostics,error}.h`;
  `src/lang/{lexer,parser,ast,types}/`, `src/util/{source,diagnostics}.cpp`. Eski `include/xlang/*.h` cephe
  başlıkları silindi.
- **Eski hat derlemeden çıkarıldı:** `src/codegen`, `src/compiler`, `src/host`, `src/platform` eski AST'ye
  bağlıydı; yeni AST ile derlenmez. xmake hedefinden çıkarıldı, dosyalar yerinde duruyor ve 4.7'de (HIR'a taşıma /
  eski yolu silme) yeniden yazılır ya da silinir. Faz 1 boyunca `xlang build/run/test` **yoktur**; yalnızca
  `xlang parse` ve `xlang --explain` vardır. LLVM bağımlılığı hedeften çıkarıldı (frontend LLVM'siz).
- **Newline anlamlıdır:** deyim ve bildirim satır sonunda biter; `;` yoktur. İfade yalnızca `(...)`/`[...]`
  içinde ya da bir ikili operatörden sonra alt satıra geçebilir (`*a = *b` satırı bir önceki `local`'in devamı
  olmaz). Lexer `Token.newline_before` taşır.
- **`#if` ailesi:** lexer direktifleri token olarak üretir (`DirIf`...); değerlendirme 3.1'dedir. Parser
  direktif görürse `E0002` ("henüz desteklenmiyor") verip bildirim/deyim sınırına senkronize olur. Yani
  `#if` içeren dosyalar Faz 3'e kadar parse edilmez.
- **Struct literal belirsizliği:** `if`/`while` koşulunda struct literal kapalıdır (`{` bloğu başlatır);
  başka yerde `Ad {` yalnızca `}` ya da `ad :` ile devam ediyorsa literal sayılır. Parantez içinde yeniden açılır.
- **Tip argümanı:** `sizeOf(i32)` gibi çağrılarda ilkel tip adı ifade olarak `Name` düğümü olur; `sizeOf([T; N])`
  gibi bileşik tipler ifade olarak değil (dizi tekrar literal'i / `*` deref ile) yorumlanır; sema'da çözülür.
- **Inline asm işlenenleri:** `in(reg) x`, `out("rax") y`, `inout(reg) z` çağrı argümanı olarak
  `Expr::Kind::AsmOperand`'a iner (`in`/`out`/`inout` adı + parantez + işlenen ifadesi). `clobber(...)` ve
  `options(...)` düz çağrıdır. Anlamı Faz 6'dadır.
- **Makro argümanları ve gövdesi:** ham token listesi olarak saklanır (`macro_args`, `DefineMacroDecl::body`);
  parametre türü doğrulaması ve parse genişleme anındadır (3.7).
- **Tip ağacı:** parser `Void/Bool/Int/Float/Pointer/Ref/FnPtr/Array/Named` üretir; `Enum/Struct/Class/Interface`
  sema'nın `Named`'i çözmesiyle dolar. `isize`/`usize` `bits = 0` (işaretçi genişliği).
- **Tanı kodları:** sözdizimi için `E0001` (sözcüksel) ve `E0002` (sözdizimi) kayıt tablosuna eklendi;
  `src/util/diagnostics.cpp` içindeki tablo tek kaynaktır, `--explain` oradan okur. Hata/uyarı sıraya konup
  kaynak konumuna göre sıralı basılır (`flush`). Hata kodlu tanılar `--allow` ile susturulamaz (CLI hatası).
  `--deny` yalnızca uyarıyı hataya çevirir; kapsam içi `@allow` `--deny`'dan önce gelir.
- **Hata toparlama:** `ParseAbort` + deyim/bildirim sınırında senkronizasyon; bir çalıştırma birden çok hata
  bildirir. Parser `@allow(KOD)` kapsamlarını bildirim/deyim span'i olarak motora kaydeder.
