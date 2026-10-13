# xlang Compiler Planı

> Dil yüzeyi (kullanıcıya görünen sözdizimi/özellikler) için bkz. **[PLAN.eg.md](PLAN.eg.md)**.
> Bu planda dil seviyesi bir şey değişirse `PLAN.eg.md` de aynı değişiklikte güncellenir.

---

## 0. Temel kurallar (her şeyden önce)

1. **Geriye ve ileriye uyumluluk YOK.** Compiler hiç kullanılmadı. Sözdizimi, AST, CLI, IR,
   dosya yapısı, her şey kökten değiştirilebilir. "Eski davranışı koru" diye bir gereksinim yok.
2. **`examples/`, `test/xlang/`, stdlib/runtime `.xlang` dosyaları tek kullanımlık** (yeni dilin
   kaynak uzantısı **`.xl`**; eski `.xlang` dosyaları yeni compiler'a uyarlanmaz). Kırılabilir,
   silinebilir. Compiler hedeflendiği gibi çalıştıktan sonra yeni dil için yeniden yazılır.
   Bu dosyalar **okunmaz**, tasarıma girdi olmaz, bir şeyi onlara uydurmak için çalışılmaz.
3. **`.cursor/` altındaki kurallar/planlar okunmaz.** Tek girdi kullanıcının yönergeleridir.
4. **Compiler yalnızca dili, CPU primitiflerini, builtin listesini ve `register` ile kayıtlı rolleri
   (`panic`, `allocator`) bilir.** Thread, scheduler, print kavramı compiler'da yoktur; bunlar xlang
   stdlib'dir. Builtin fonksiyon = **gövdesini compiler'ın kendisinin sağladığı** fonksiyon (liste kural
   7'de). `memcpy/memset/memmove/memcmp`, `malloc`/`free` ve allocator builtin değildir, stdlib'dendir;
   `new` allocator'ı kayıtlı `register(allocator)` global'inden ya da açıkça verilen `a`'dan alır
   (4.11, 4.12). Compiler doğrudan destekleyemeyeceği bir şeyi builtin yapmaz; o şey stdlib'e gider.
   **Compiler stdlib'e özel muamele yapmaz**: stdlib sıradan xl kodudur, `register` her modülde aynı çalışır.
5. **Önce compiler doğru çalışsın.** Stdlib/example işleri compiler hedefine ulaştıktan sonra gelir.
6. **Plan ve dil şeması senkron kalır:** dil seviyesini etkileyen her plan değişikliği
   `PLAN.eg.md`'yi de günceller (bkz. `CLAUDE.md`).

7. **Global isim uzayı kapalıdır ve iki katmanı vardır.** Kaynak koduna "bedavaya" (import'suz) gelen
   isimler yalnızca (a) compiler'ın bildiği builtin'ler ve (b) bir modülde `register` ile kaydedilmiş
   semboller (4.12) olabilir. Builtin listesi (kesin hali Faz 6'da netleşir):

   | Grup | Adlar | Yazım |
   |------|-------|-------|
   | Syscall | `syscall` | öneksiz, doğrudan `syscall` |
   | Atomic | `__builtin_atomic_load/store/xchg/cas/add`, `__builtin_fence` | `__builtin_` önekli |
   | Inline asm | `__builtin_asm` | `__builtin_` önekli |
   | Memory order | `MemoryOrder` (enum tipi: `MemoryOrder.relaxed`, `.acquire`, `.release`, `.acq_rel`, `.seq_cst`) | öneksiz tip |
   | Derleme zamanı | `size_of`, `align_of`, `offset_of`, `static_assert` | öneksiz |

   Builtin fonksiyonlar iki biçimde yazılır ve **hepsi `__builtin_` önekini taşımaz**.
   `register` ile kaydedilenler (ör. stdlib'in `panic`'i) builtin değildir: compiler onları stdlib'den
   değil kayıttan tanır, `register` hangi modülde olursa olsun aynı çalışır.
   `print`, `alloc`, `memcpy`, `malloc`/`free`, `assert` gibi hiçbiri builtin de `register`'lı da değilse
   global değildir; stdlib'den `import` ile gelir.
   **libc yasaktır:** hiçbir yerde kullanılmaz, çağrılmaz, taklit edilmez.
   `declare` yalnızca kullanıcının kendi bağladığı bir nesneden (ör. `api.o`) gelen dış şeyler içindir
   (fonksiyon, global, struct, class, interface, type); xl modülleri arası erişim `import`/`export` iledir.

---

## 1. Hedef hat

```
Source ─► Lexer ─► Preprocess (#if/#elif/#else/#endif, token seviyesi) ─► Parser ─► AST ─► Macro expand ─► Module resolve
       ─► Sema (tip, isim, layout) ─► HIR ─► LLVM IR ─► Optimize ─► Object ─► Link
```

## 2. Tasarım kararları (özet)

| Konu | Karar | Neden |
|------|-------|-------|
| Syscall | Builtin `syscall(nr, args...)` (yapıldı); argüman sayısı **dinamik**: compiler verilen sayıyı olduğu gibi iletir, sabit sınır yok. Derleme hedefinin OS/arch'ına göre izin verilen en fazla argüman sayısı aşılırsa **derleme hatası** | Compiler hangi syscall olduğunu bilmez; sınır OS'a göre değişir |
| Fonksiyon sözdizimi | `fn name(a: i32) -> i32 { }`; `->` yoksa dönüş `void` | İstek: `=>` değil `->` |
| Tamsayı tipleri | `i8 i16 i32 i64 isize` / `ui8 ui16 ui32 ui64 usize` | İşaretli/işaretsiz ayrımı; kullanıcı yazımı `ui32` |
| Kayan nokta | `f32`, `f64` | |
| Diğer tipler | `bool`, `void`, `*T` (pointer), `fn(A) -> R` (fonksiyon pointer'ı), `[T; N]` (sabit boyutlu dizi), `struct`, `class`, `interface`, `enum` | |
| Örtük dönüşüm | Sadece aynı işaretlilikte genişleme; diğer her şey açık `as` | Sessiz işaret/daralma hataları olmasın |
| Taşma | İşaretli ve işaretsiz tamsayılar sarar (two's complement), bölme sıfıra = `panic` (kayıtlı `panic` yoksa trap) | Tanımlı davranış |
| Bellek düzeni | Varsayılan: **xlang düzeni** (derleyici alan sırasını/padding'i seçebilir). `@repr(c)` ile işaretli struct/class **C/C++ düzenine** birebir uyar | İstek |
| Class | Tek kalıtım, sanal metod, vptr; `@repr(c)` ile Itanium C++ ABI düzeni (tek kalıtım) | İstek: compiler class bilsin |
| Interface | Fat pointer `{data, itable}` (Go/Rust tarzı); struct ve class implement edebilir | Düzeni (C uyumu) bozmaz |
| Import | `import "m" as a`, `from "m" import x` **ve** `import x from "m"` (ikisi de geçerli, aynı AST) | İstek: iki sıra da desteklensin |
| Koşullu derleme | Satır tabanlı direktifler `#if/#elif/#else/#endif/#ifdef/#ifndef/#error`; koşul, dilin normal `if` ifadesiyle aynı sözdizimi ve operatörler (`defined(X)`, `!defined(X)`, `&& \|\| !`, karşılaştırma), derleme zamanında basit yorumlayıcıyla değerlendirilir. **Gövde aynı satırdaysa `#endif` gerekmez**, alt satıra inerse `#endif` zorunlu. Pasif bölge yalnızca lex edilir | İstek |
| Dış kütüphane / stdlib | Dil dışarıdan kütüphane almaz: libc, libSystem, kernel32, pthread kütüphanesi yok. Stdlib her OS'un çekirdek ABI'sini `#if os` ile ham `syscall` üzerinden çağırır. Stdlib isteğe bağlı: `--no-std`, `--std=<yol>`; varsayılan stdlib dile gömülü, içinde Go benzeri scheduler var | İstek |
| libc | **YASAK.** Hiçbir koşulda kullanılmaz. xl'in kendi stdlib'i vardır | İstek |
| Builtin fonksiyonlar | Global, compiler'ın **gövdesini kendisinin sağladığı** fonksiyonlar. **Liste:** atomic'ler, `fence` ve inline asm (`__builtin_` önekli). `memcpy/memset/memmove/memcmp` (LLVM'in örtük çağrıları için C adıyla bulunmalı) ve `malloc/free`/allocator builtin değildir, stdlib'dendir (`std/memory`, `std/mem`). Compiler doğrudan desteklemeyecekse builtin olmaz. İki yazım: `__builtin_xx` (atomic'ler, `fence`, `__builtin_asm`) ve öneksiz (`syscall`, `size_of` ailesi, `static_assert`); `syscall` her zaman doğrudan `syscall` | İstek |
| Tanılar | Compiler kısıtlamaz, uyarır: her uyarı `ne + detay + neden + nasıl düzeltilir`; kodlu (`W0201`...), `--allow/--deny/@allow` ile susturulur veya hataya çevrilir, `--explain` | İstek |
| Düzen geçmişi | Varsayılan düzen **sürümlü şema** (v1, v2, v3...), eski şemalar donmuş ve compiler'da kalır; `.o/.a/.bc` düzen kaydı taşır; `xl.layout` proje geçmişi; bilerek karıştırma (`@layout(shuffle)`) kayıtta işaretlenir; karıştırılmamışsa düzen yeniden türetilir | İstek: önceki derleyici çıktılarının düzenini bilme |
| `declare` | Tek önek; her bildirimin başına konur: `declare fn/global/struct/class/interface/type`. Global için ayrı sözdizimi yok. Öznitelikler normal bildirimle aynı (`@repr(c)`, `@packed`, `@align`, `@mangle`); `@repr(c)` `declare`'de **zorunlu değil**; düzen yoksa kayıt → geçmiş → C varsayımı + uyarı. Dış tarafın xl/C++/Rust/`.o`/statik olması kısıtlanmaz | İstek |
| Modül yolları | `"std"` → `std.xl` ya da `std/index.xl`; `"std/memory"` aynı kuralla. Giriş dosyası `index.xl` | İstek: `import "std"` hepsini versin |
| Çift import / çift export | Aynı ada sahip iki bağlama hatadır, **import/export satırında** raporlanır: `import * from a` + `import * from b` (ortak ad), çift adlandırılmış import, yerel tanımla çakışma. `import * as aa from a` + `import * as bb from b` çalışır. Aynı varlığın iki yoldan gelmesi hata değil ama **detaylı uyarı** (`W0401`) + log | İstek |
| Yeniden dışa aktarma | `export * from "m"`, `export * as ns from "m"`, `export { a, b as c } from "m"`, `export * from "m" except a, b`, `#if` içinde koşullu | İstek: JS benzeri ama daha geniş |
| Kaynak uzantısı | `.xl` | İstek |
| Görünürlük | **Varsayılan export yok.** `export` yazılmayan sembole başka modül erişemez; link'te de internal. `export(c)` = `export` + mangle edilmemiş C ABI sembolü | İstek: C-like export, kendi modülleri arası kullanım |
| Name mangling | Varsayılan açık (`xl` şeması). Proje (`--mangle=`), dosya (`@file(mangle = ...)`), fonksiyon (`@mangle(...)`) seviyesinde override; öncelik fn > dosya > proje | İstek |
| Bellek güvenliği | Ham `*T` C gibi kontrolsüz kalır; ek `ref T` = `{ptr, gen}` nesil kontrollü güvenli referans (use-after-free'de `panic`). Borrow checker / lifetime anotasyonu yok. `--safety=checked\|unchecked`, `@unchecked fn` | İstek: Rust benzeri koruma, dil kalıbını bozmadan |
| C++ birlikte kullanım | Compiler yalnızca `obj` / `ir` (`.ll`) / `bc` / `header` / `exe` / `static` / `shared` üretir; bağlama ve build'i kullanıcının script'i (xmake vb.) yürütür | İstek: compiler karar vermesin |
| Tip takma adı | `type Ad = Tip` (şeffaf alias, yeni tip yaratmaz), `export type` | İstek: typedef benzeri |
| define / macro | `define N = sabit` ve `define f(x: expr) { ... }`, çağrı `f!(...)`; gövde süslü parantezli, `\` yok, `#`/`##` yok, türlü parametre (`expr/type/ident/block/items`), hijyenik; macro bildirim ve `import` üretebilir, `export define` ile import edilebilir | İstek: C önişlemcisinin pisliği olmadan macro |
| CLI tanımları | `-D/--define NAME[=DEĞER]`, `-U/--undef NAME`, + yerleşik `os`, `arch`, ... | İstek |
| Thread/scheduler/IO/alloc | xlang stdlib; compiler görmez (Faz 7). İstisna: `register` ile kayıtlı roller (aşağıda) | Compiler yalnızca düşük seviye bilir |
| `register` | `register(rol) bildirim` / `register bildirim`: `declare` gibi tek önek, ters yönde: bildirilen ad modül derlemeye girince **global** olur (import gerekmez, `export` da sayılır). Roller: `panic`, `allocator`; rol başına en fazla bir kayıt. Gölgeleme: yerel tanım/import kayıtlı adı gölgeler (`W0402`). Compiler stdlib'i ayırt etmez | İstek: stdlib'in `panic`'i import'suz görünsün, compiler otomatik çağırsın |
| Panic / assert | `register(panic) fn panic(msg: *ui8)` `std/runtime`'da; compiler'ın eklediği hata yolları (sıfıra bölme, `ref` nesil uyuşmazlığı, dizi sınırı) onu çağırır, kayıtlı yoksa trap. `assert!` `std/assert`'te macro | İstek |
| Allocator / `new` | `new T { }` (kayıtlı allocator) ve `new(a) T { }` (açık). Allocator = `alloc` + `free` içeren grup (`std/mem`'de `Allocator` interface'i); bırakma aynı gruptan. Kayıtlı allocator `main`'de `set_allocator` ile **bir kez** çalışma zamanında değiştirilebilir | İstek |
| Enum | `enum Ad: T { A, B = 5 }`; `T` sabit değer alabilen herhangi bir tip (tamsayı, `f32/f64`, `bool`, `*ui8` string); yazılmazsa `i32`; erişim `Ad.A`; dönüşüm açık `as` | İstek |
| Dizi | `[T; N]`; indeksleme `a[i]` (dizi ve `*T`); dizide sınır kontrolü `--safety=checked` (varsayılan) altında `panic`; değer tipi; literal `[a, b]` ve `[v; N]` | İstek: C struct uyumu + güvenli indeksleme |
| Pass manager | New PassManager (`PassBuilder`) | Legacy PM deprecated |
| Derleme modeli | Whole-program, tek LLVM modülü; ayrı derleme en sona | Basit, doğru |
| HIR | Eklenecek | Sema çıktısı; codegen yalnızca tipli ağacı görür |
| LIR | Eklenmeyecek | LLVM IR zaten LIR; ikinci düşük seviye IR kazanç sağlamaz. Koşul: ikinci backend ya da dile özgü CFG analizi → o zaman MIR |

### Karar bekleyen / sonradan netleşecek

- `ui32` yazımı kullanıcı isteğiyle seçildi; `u32`'ye çevirmek yalnızca tip tablosunu değiştirir.
- Generic'ler ve closure kapsam dışı (ilk sürümde yok).
- **Önek kuralı:** atomic'ler, `fence` ve `__builtin_asm` `__builtin_` önekli; `syscall`, `size_of` ailesi,
  `static_assert` öneksiz. İleride eklenecek builtin için ölçüt yazılmalı, yoksa her ekleme ayrı karar olur.
- **Liste sınırı:** builtin listesi kapalıdır (kural 7); eklenecekler "compiler gövdeyi doğrudan
  sağlayabiliyor mu" ölçütüne tabidir.
- **Builtin adı çakışması:** kullanıcı/stdlib bir builtin adını (ör. `size_of`) tanımlarsa hata mı,
  gölgeleme mi? (`register` adları için gölgeleme `W0402` ile karara bağlandı.)
- **`new` ve `ref T`:** `new T{}` `*T` döndürür; `ref T`'ye atamak için `gen` başlığı (4.10) gerekir:
  kayıtlı allocator'ın ref-uyumlu olması mı, ayrı bir yazım mı belirlenmedi (§17 örneği `new Node{}`'u
  `ref Node`'a atıyor).
- **`syscall` argüman tipleri:** pointer'ın `as` olmadan geçip geçmeyeceği (5.5 "syscall sınırında" der)
  örneklerde tutarsız yazıldı; tek kural yazılmalı.
- **String literal:** `panic`/`print` string'in `*ui8` olduğunu ve sonunun 0 ile bittiğini varsayıyor;
  string literal'in dil seviyesinde kuralı yazılmadı, uzunluk taşıyan ayrı bir `str` tipi planlanmadı.
- **`set_allocator` sınırı:** ilk `new`'den sonra çağrılırsa önceki bloklar yeni allocator'a bırakılabilir;
  bu tespit edilmez. Kural: `main`'in başında çağrılır (belgelendi); ileride tespit eklenebilir.
- **`panic` imzası:** dosya/satır konumu iletilmiyor; ileride `register(panic)`'e ek parametre
  eklenebilir. `msg` şu an sabit NUL'lu string.
- **Dizi ayrıntıları:** uzunluk için ayrı builtin yok (`size_of(a) / size_of(T)`); dilim (slice) tipi yok;
  sabit indeksin sınır dışı olması derleme hatası mı uyarı mı belirlenmedi.
- **Enum ayrıntıları:** string enum'da değerden üyeye ters arama yok; `export(c)` imzasında yalnızca tamsayı
  alt tipli enum geçerli sayıldı; enum'un `@repr(c)` düzeni alt tipin düzenidir.
- **`register` ayrıntıları:** `define` (macro) kaydı kapsam dışı; `register` ile kaydedilen sembolün
  başka bir kaydı gölgelemesi/çakışması `E0502`; rol sayısı (şu an `panic`, `allocator`) yeni ihtiyaçta
  genişler, her yeni rol bu listeye ve 4.12'ye yazılmalı.

---

## Faz 0 — Temizlik ve güvenlik ağı

- [ ] **0.1** `llvm::verifyModule()` — `generate.cpp`'de `release()` öncesi; hata → IR dump + hata.
- [ ] **0.2** Eski test/example'a bağımlı hiçbir build adımı kalmasın; derleyici kendi küçük
  IR/parse testleriyle doğrulanır (`test/compiler/`, yeni).
- [ ] **0.3** Ölü kod: eski dil yüzeyinin compiler'daki izleri (`declare syscall`, `@syscall`,
  `=>` ya da `:` dönüş tipi, `BigInt`/`Char`/`String` gibi kaldırılacak tipler) planlı olarak
  Faz 1–2'de silinir; eski sözdizimi için uyumluluk katmanı yazılmaz.

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

## Faz 2 — Tip sistemi ve bellek düzeni (layout)

- [ ] **2.1** Tip modeli: boyut/hizalama (`size_of`, `align_of`) hedefe (`DataLayout`/pointer genişliği) göre hesaplanır.
- [ ] **2.2** İşaretli/işaretsiz semantik: `sdiv/udiv`, `srem/urem`, `sext/zext`, `icmp slt/ult`,
  `ashr/lshr` — tip bazlı seçim. Örtük dönüşüm kuralları sema'da (tek yerde). Sıfıra bölme kayıtlı
  `panic` rolünü çağırır (kayıtlı yoksa trap).
- [ ] **2.3** Layout motoru (yeni `src/sema/layout.cpp`):
  - `@repr(c)`: alanlar bildirim sırasında, C kurallarıyla hizalama/padding, `sizeof` = C `sizeof`.
  - varsayılan: derleyici serbestçe sıralayabilir (ör. büyükten küçüğe) — spesifikasyon verilmez.
  - `@packed`, `@align(N)` desteği.
  - Üretilen LLVM struct tipleri, hesaplanan offset'lerle birebir uyuşur (gerekirse açık padding alanı).
- [ ] **2.3b** Düzen şemaları ve geçmiş (`src/sema/layout_history.cpp`):
  - Varsayılan düzen, sürümlü ve deterministik **şema** (v1, v2, v3...); eski şemalar donmuştur ve
    compiler'da kalır, yeni davranış yeni şema olur. `--layout-scheme=`, `@layout(scheme = vN)`.
  - Dışa aktarılan tipler için **düzen kaydı** (şema kimliği, alan sırası, offset, boyut, hizalama,
    karıştırıldı/seed bayrağı) `.o`/`.a`/`.bc`/`.ll` içine metaveri olarak yazılır (LLVM named metadata
    `!xl.layout`, nesnede ayrı bölüm).
  - Tüketici tarafta (`declare` veya xl nesnesi) kayıt okunur; `xl.layout` proje geçmişi de aynı biçimde.
    Karıştırılmamış düzen şema + tip tanımından yeniden türetilir; `@layout(shuffle)` kayıtta işaretlenir.
  - Kayıt yok ve `@repr(c)` yok → C varsayımı + `W0201`; kayıtlı düzenle çelişki → `W0202`;
    şema uyuşmazlığı → `W0203`.
  - CLI: `--layout-history=`, `--layout-record`, `--print-layout <Tip>`.
- [ ] **2.4** Derleme zamanı sorguları: `size_of(T)`, `align_of(T)`, `offset_of(T, alan)`; `static_assert(cond, "msg")`.
  C başlığıyla layout doğrulaması bunlarla yapılır.
- [ ] **2.5** Sabit boyutlu dizi tipi `[T; N]` (C struct uyumu için): `N` derleme zamanı sabit ifadesi
  (`define` sabiti olabilir); `size_of([T; N]) = N * size_of(T)`, hizalama `align_of(T)`; yerel, struct/class
  alanı ve global olarak kullanılabilir; `@repr(c)` struct içinde C'deki `T[N]` ile aynı düzen.
  - **Değer tipi:** atama, parametre ve dönüşte tamamı kopyalanır; kopyayı önlemek için `&a`.
  - **Literal:** `[a, b, c]` ve tekrar biçimi `[v; N]`; eleman tipi bağlamdan çıkarılır.
  - **İndeksleme `a[i]`:** dizi ve `*T` üzerinde; indeks `usize` (aynı işaretlilikte genişleme örtük,
    işaretliler `as usize`). `*T` indekslemesi `*(p + i * size_of(T))` ve **kontrolsüzdür**. Dizi
    indekslemesi `--safety=checked` (varsayılan) altında sınır kontrolü yapar, dışındaysa kayıtlı `panic`'i
    çağırır; `unchecked` ya da `@unchecked fn` kontrolü kaldırır. Sabit indeks sınır dışıysa derleme hatası.
  - Global başlangıç değerleri ve kuralları PLAN.eg.md §4 "Global değişkenler"de.
- [ ] **2.6** `enum Ad: T { Üye [= değer], ... }`: `T` derleme zamanı sabiti olabilen herhangi bir tip
  (tamsayılar, `f32/f64`, `bool`, `*ui8` string literal'i); `T` yazılmazsa `i32`. Üyeler `Ad.Üye` ile
  erişilir. Otomatik numaralandırma yalnızca tamsayı alt tipte (önceki + 1, ilk üye 0); diğer alt tiplerde
  her üyenin değeri yazılmalıdır. `Ad` ayrı bir tiptir: alt tipe/alt tipten dönüşüm açık `as`; `==`/`!=`
  aynı enum'da geçerli. Değerler derleme zamanı sabitidir (`static_assert`, global başlangıç değeri,
  atomic `order` argümanı için kullanılabilir). Layout alt tipin düzenidir; `export(c)` imzasında yalnızca
  tamsayı alt tipli enum kabul edilir. `MemoryOrder` (6.1) bu mekanizmayla tanımlı builtin enum'dur.

**Kabul:** `@repr(c)` struct'lar için clang'ın ürettiği `sizeof/alignof/offsetof` ile test
(birkaç karma tipli struct, iç içe, packed, aligned) birebir uyuşur; x86_64 ve aarch64 için.

## Faz 3 — Koşullu derleme, define/macro ve CLI tanımları

- [ ] **3.1** Önişleme (`src/lang/preprocess/`): lexer'dan sonra, parser'dan önce, token akışı üzerinde.
  Direktifler satır başında `#if`, `#elif`, `#else`, `#endif`, `#ifdef`, `#ifndef`, `#error`.
  - **Tek satır biçimi:** koşul ve gövde aynı satırdaysa `#endif` yoktur (gövde o satırın kalanıdır).
  - **Blok biçimi:** gövde alt satırdan başlıyorsa `#endif` zorunlu; `#elif`/`#else` yalnızca blok biçiminde.
  - Pasif bölge yalnızca lex edilir; parse/sema/codegen'e girmez. İç içe direktifler desteklenir.
  - Koşul/gövde sınırı: koşul ifadesinin bittiği token; belirsizlikte koşul parantezlenir.
  - Item, statement, struct/class alanı ve `export ... from` seviyesinde çalışır (token akışı olduğu için
    konumdan bağımsız).
- [ ] **3.2** Koşul ifadesi: dilin normal ifade sözdizimi ve operatörleri, derleme zamanı sabit ifade olarak
  basit yorumlayıcıyla değerlendirilir: `&& || !`, `== !=`, `< <= > >=`, tamsayı/string/bool sabitleri,
  `defined(NAME)` / `!defined(NAME)`; `#ifdef NAME` ve `#ifndef NAME` bunların kısaltmasıdır.
- [ ] **3.3** Yerleşik tanımlar (hedeften türer): `os` (`"linux"|"macos"|"windows"`), `arch`
  (`"x86_64"|"aarch64"`), `pointer_width` (`32|64`), `endian` (`"little"|"big"`), `debug` (bool),
  `opt_level` (0..3).
- [ ] **3.4** CLI bayrakları (`src/cli`):

  | Bayrak | Anlam |
  |--------|-------|
  | `-D NAME`, `--define NAME` | `NAME = true` |
  | `-D NAME=DEĞER`, `--define NAME=DEĞER` | `NAME` tamsayı/string/bool değeri |
  | `-U NAME`, `--undef NAME` | Tanımı (yerleşik dahil olmayan) kaldırır |
  | `--target <triple>` | Hedef triple; `os/arch/pointer_width` buradan türer |
  | `-O0..-O3` | Optimizasyon seviyesi (`opt_level`) |
  | `-g` | Debug info (`debug = true`) |
  | `--emit=ir\|bc\|obj\|hir\|header\|exe\|static\|shared` | Çıktı biçimi/aşaması |
  | `--mangle=xl\|none` | Proje seviyesi mangling varsayılanı |
  | `--safety=checked\|unchecked` | `ref T` kontrolleri |
  | `--print-cfg` | Çözülmüş tüm tanımları yazdır |
  | `--cpu <name>` | Hedef CPU (varsayılan `generic`) |
  | `--no-std` / `--std=<yol>` / `--std-features=...` | Stdlib seçimi (varsayılan: gömülü stdlib) |

- [ ] **3.5** `#error "mesaj"`: koşul dalı seçilirse derleme hatası.
- [ ] **3.6** `define` sabitleri: dosya içi `define N = ifade` ve CLI `-D` aynı isim uzayında; CLI öncelikli;
  `#if` ve normal kodda okunabilir. Sabit ifade değerlendirici (tamsayı/bool/string) burada yazılır.
- [ ] **3.7** Macro genişletme (`src/lang/macro/`): token ağacı üzerinde, metin birleştirme yok.
  - Parametre türü doğrulama (`expr`, `type`, `ident`, `block`, `items`).
  - Hijyen: macro içi `local` adları taze isimle yeniden adlandırılır.
  - Genişleme bir fixpoint döngüsüdür (macro → `#if` eleme → tekrar macro), derinlik sınırı 64,
    hatalar genişleme zincirini ve çağrı konumunu gösterir.
  - Macro gövdesi item/statement/`import` üretebilir; modül seviyesinde çağrılan macro bildirim üretir.
  - `export define` ve `from "m" import ad` ile modüller arası macro paylaşımı; modül yükleme sırasında çözülür.
  - `#`/`##` ve satır devam karakteri sözdiziminde **yoktur** (`stringify!` da yoktur).
- [ ] **3.8** `type` takma adları sema'da çözülür (Faz 4.1 scope'unda), HIR'da hedef tipe indirgenir;
  döngüsel takma ad hatası.

**Kabul:** `-D DISABLE_X` ile bir item/alan/statement elenir; `--print-cfg` doğru değerleri gösterir.

## Faz 4 — Sema ve HIR

- [ ] **4.1** Sembol tablosu/scope: modül, fonksiyon, blok; nitelikli isim (`alias.name`).
- [ ] **4.2** Sema: isim çözümleme, tip kontrolü, overload yok/var kararı (önerim: overload yok, ad bazlı
  tek tanım, mangling basit), `syscall(...)` argüman doğrulaması (nr + argümanlar tamsayı/pointer; sayı sabit değil, hedef OS/arch'ın
  en fazla argüman sayısına göre 5.3'teki tablodan denetlenir, aşılırsa hata), `static_assert`.
- [ ] **4.3** HIR (`include/xlang/hir/hir.h`): tipler çözülmüş, örtük dönüşümler açık `Cast`,
  sugar düşürülmüş, sınıf metodları `fn(self: *T, ...)`, interface çağrıları açık itable çağrısı.
- [ ] **4.4** Class:
  - Tek kalıtım, `class B : A`; `virtual` metod, `override`; vptr nesnenin başında.
  - `@repr(c)` class: Itanium düzeni (vptr offset 0; vtable'da offset-to-top ve typeinfo slotları,
    ardından fonksiyonlar). Çoklu/sanal kalıtım ve MSVC ABI kapsam dışı (açık hata).
  - Constructor/destructor: ilk sürümde açık `init`/`deinit` metodları.
- [ ] **4.5** Interface: bildirim, `impl Ad for Tip { ... }` (struct ve class implement edebilir),
  interface değeri = `{data: *void, itable: *T}`;
  statik tipten arayüze dönüşüm itable'ı derleme zamanında üretir; çağrı = itable'dan dolaylı çağrı.
- [ ] **4.6a** Yol çözümleme: `"x"` → `x.xl` | `x/index.xl` (ikisi birden hata); yeniden dışa aktarma
  grafiği tek geçişte çözülür; çakışma kullanım yerinde hata, yerel export önceliklidir; export edilmeyen
  sembol yeniden dışa aktarılamaz (4.8 ile uyumlu); `export(c)` aktarmada korunur.
  **Çift import da aynı kuralla hatadır** (`import * from a` + `import * from b` ortak ad → import satırında
  hata; `import * as aa` / `import * as bb` serbest; aynı varlık iki yoldan gelirse hata değil, `W0401` uyarısı + log; farklı varlıklar `E0401` hatası).
  **Aynı ada sahip iki export hatadır** (aynı dosyada çift export, farklı dosyalardan aynı ad, `export *`
  ile gelen ve yerel olan dahil); "yerel kazanır" kuralı yoktur. Çözüm yolları: `except`, `as`, `export * as ns`.
  Modül çözümleme stratejileri (birden fazla yol: göreli, proje kökü, arama yolları, std, paket) **henüz
  planlanmadı**; bu madde yalnızca dosya/dizin kuralını (`x.xl` | `x/index.xl`) kapsar.
- [ ] **4.6b** `declare` semantiği: gövdesiz dış öğe, kod üretmez; düzen çözümü sırası: nesne içi düzen kaydı → `xl.layout`
  geçmişi → C varsayımı + `W0201`; `@repr(c)` zorunlu değil; opak tipler yalnızca pointer ile kullanılır; `declare class` metodları dış sembol (`@mangle` ile ad verilir).
- [ ] **4.6** Modül yükleme: AST klonlama yerine modül başına `Program` + sembol tablosu; mangling tek yerde.
- [ ] **4.7** Codegen'i HIR'a taşı; `resolve.cpp`/`print.cpp`/`expr.cpp`'deki tip çözümleme mantığı
  sema'ya gider. Eski AST→LLVM yolu silinir (yan yana yaşatılmaz).

- [ ] **4.8** Görünürlük: `export` olmayan hiçbir sembole (fonksiyon, tip, global, `define`, `type`)
  başka modülden erişilemez (sema hatası); LLVM linkage'ı `InternalLinkage`. `export` →
  `ExternalLinkage` (xl mangling'li); `export(c)` → `ExternalLinkage`, C ABI adı.
  `export(c)` imzasında yalnızca C uyumlu tipler (tamsayılar, `f32/f64`, `bool`, `*T`, `@repr(c)` struct,
  fonksiyon pointer'ı, tamsayı alt tipli enum); `class`/`interface`/`ref T`/string enum hata (`E0302`).
- [ ] **4.9** Name mangling: tek modül (`src/sema/mangle.cpp`) — `xl` şeması (`_xl$modül$ad$imza`);
  `@file(mangle = ...)`, `@mangle(none|"ad")`, `--mangle=`; öncelik fn > dosya > proje.
  Mangling kapalıyken sembol çakışması derleme hatası.
- [ ] **4.10** `ref T`: `{ptr, gen}` 16 B; allocator `gen` başlığını yönetir (stdlib `std/mem`), compiler
  her erişimde `ref.gen == obj.gen` kontrolü yapar, uyuşmazlıkta kayıtlı `panic`'i çağırır (kayıtlı yoksa
  trap). `&local`'dan ref'in kaçışı derleme hatası
  (basit escape kontrolü, borrow checker değil). `@unchecked fn` ve `--safety=unchecked` kontrolleri kaldırır.
  `ref T` `@repr(c)` alanında ve `export(c)` imzasında yasak.
- [ ] **4.11** `new T { alanlar }` ve `new(a) T { alanlar }`: dil ifadesi; `a.alloc(size_of(T),
  align_of(T))` çağırır, alanları yazar ve `*T` döndürür. Allocator bir **gruptur**: `alloc(self, size:
  usize, align: usize) -> *void` ve `free(self, p: *void)` metodlarının ikisine de sahip herhangi bir değer
  (class, interface değeri, `impl` eden struct); sema bu iki metodu çözer (`E0503`: eksikse). Bellek
  ayıran ve bırakan hep aynı gruptır. `new T` **kayıtlı allocator**'ı kullanır: `register(allocator)`
  ile kaydedilmiş global (4.12). Compiler adı değil kaydı arar, `malloc` bilmez (kural 4) ve stdlib'i
  ayırt etmez: öncelik kuralı yoktur. Programda toplamda **en fazla bir** `register(allocator)` olabilir
  (iki tane `E0502`). Stdlib `std/mem`'de bir tane kaydeder (stdlib özelliği `default-allocator`,
  varsayılan açık); kullanıcı kendi allocator'ını `--std-features=-default-allocator` ile özelliği kapatıp
  kaydederek ya da çalışma zamanında `main`'de `std/mem.set_allocator(a)` ile **bir kez** değiştirerek
  seçer; kayıtlı global mutable olduğundan `new` ve `std/mem.free` değişimden sonra yeni allocator'ı
  kullanır. Kayıtlı yoksa `new T` hatadır (`E0501`, düzelt: `new(a) T` ya da `register(allocator)`).
  `alloc` `null` dönerse `new` `null` döndürür.
- [ ] **4.12** `register[(rol)] bildirim`: `declare` gibi tek önek. Bildirilen ad, modül derlemeye girer
  girmez **global** olur (import gerekmez; `register` `export` anlamına da gelir). Kayıtlar modül
  yüklenirken toplanır: yalnızca import grafiğindeki modüller ve stdlib açıkken her zaman yüklenen
  `std/runtime` sayılır; yüklenmeyen modülün kaydı yoktur. Roller:
  - `panic`: `fn(msg: *ui8)`; geri dönmez (compiler çağrıdan sonrasını ulaşılamaz sayar). Compiler'ın
    ürettiği hata yolları (2.2 sıfıra bölme, 2.5 dizi sınırı, 4.10 `ref` uyuşmazlığı) onu çağırır; kayıtlı
    yoksa doğrudan trap talimatı üretir.
  - `allocator`: 4.11'deki grup sözleşmesini sağlayan **global değişken**; `new T` onun `alloc`'unu çağırır.
  Hata kodları: `E0502` (aynı rolün/adın ikinci kaydı), `E0503` (rol sözleşmesi sağlanmadı). Aynı adlı
  yerel tanım/import kayıtlı adı gölgeler ve `W0402` uyarısı verir; rol bağı addan değil kayıttan
  olduğundan gölgeleme rolü bozmaz. Compiler `register`'ı modül stdlib'den mi kullanıcıdan mı geliyor diye
  ayırt etmez.

**Kabul:** class/interface/vtable içeren küçük programlar çalışır; `@repr(c)` class'ın nesne düzeni
ve vtable'ı clang ile birebir uyuşur (tek kalıtım).

## Faz 5 — Codegen kalitesi

- [ ] **5.1** Alloca'lar fonksiyon entry bloğuna (`mem2reg` için).
- [ ] **5.2** `PassBuilder` ile `buildPerModuleDefaultPipeline(level)`; `CodeGenOptLevel` bağla; `--cpu`.
- [ ] **5.3** Syscall hedef OS ayrımı (`src/codegen/target.cpp`): compiler `syscall(nr, args...)`'ı
  hedefin (OS + arch) çağrı kuralına çevirir; hangi syscall olduğunu bilmez. **Argüman sayısı dinamiktir:**
  compiler kaç argüman verildiyse o kadarını iletir (3 verildiyse 3), eksikleri sıfırlamaz/doldurmaz.
  Her hedef için tabloda bir **en fazla argüman sayısı** vardır; kod üretilmeden önce `--target`'ın OS/arch'ına
  göre denetlenir ve aşılırsa derleme hatası verilir (hata mesajı hedefi ve sınırı söyler).

  | Hedef | Komut | Numara | Argümanlar | Not |
  |-------|-------|--------|------------|-----|
  | Linux x86_64 | `syscall` | `rax` | `rdi rsi rdx r10 r8 r9` | |
  | Linux aarch64 | `svc #0` | `x8` | `x0..x5` | |
  | macOS x86_64 | `syscall` | `rax` (`0x2000000 +` sınıf) | `rdi rsi rdx r10 r8 r9` | BSD sınıfı |
  | macOS aarch64 | `svc #0x80` | `x16` | `x0..x5` | |
  | Windows x86_64 (NT) | `syscall` | `eax` (SSN) | `r10 rdx r8 r9`, fazlası stack (`[rsp+0x28..]`) | 4'ten fazla argümanda compiler stack'i hazırlar |

  Windows NT syscall numaraları (SSN) sürüme göre değişir; numara tablosu stdlib'dedir ve `-D` ile seçilir
  (compiler tabloyu bilmez). Desteklenmeyen OS/arch için açık hata. En fazla argüman sayısı yalnızca bu
  tablodan gelir (register ile geçenler tablodaki sütunlardır; Windows'ta stack aktarımıyla daha yüksek);
  başka yerde sabit bir sayı yazılmaz.
  **Dönüş sözleşmesi (hedefler arası tek biçim):** `syscall` `i64` döner ve hatayı **negatif `-errno`**
  olarak bildirir. Linux zaten böyledir; macOS carry bayrağını set edip pozitif errno döndürdüğü için
  compiler carry set ise sonucu `-errno`'ya çevirir (dönüş değeri ABI'sidir, hangi syscall olduğunu bilmek
  gerekmez). Windows'ta NTSTATUS (`eax`) olduğu gibi dönüş değeridir, `i64`'e işaret genişletilir.
- [ ] **5.4** Debug info (`DIBuilder`): compile unit, subprogram, satır konumları, struct/class tipleri.
- [ ] **5.5** Pointer↔integer dönüşümleri yalnızca `syscall` sınırında ve açık `as`'ta.
- [ ] **5.6** Çıktı biçimleri: `--emit=obj` (nesne), `ir` (`.ll`, `clang++` ile derlenebilir; hedef triple
  ve data layout yazılı), `bc` (bitcode). Kaynak: yalnızca LLVM Module; farklı yazıcılar.
- [ ] **5.7** `--emit=header`: `export(c)` fonksiyonları ve `@repr(c)` tipler için `extern "C"` korumalı
  C/C++ başlığı (`src/codegen/header.cpp`). Compiler başka bir şey üretmez/bağlamaz.
- [ ] **5.8** `--emit=static` (arşivle) ve `--emit=shared` (paylaşımlı kütüphane): yalnızca kolaylık;
  obj/ir üretimiyle karıştırılmaz. Kullanıcının kendi build script'i (xmake) obj/ir'i alıp kendisi de
  bağlayabilir; compiler bunu zorunlu kılmaz.

## Faz 6 — Düşük seviye primitifler (thread-agnostic)

Stdlib'in ihtiyacı olan minimum; hiçbiri "thread" kavramı taşımaz:

- [ ] **6.1** Atomic builtin'ler: `__builtin_atomic_load/store/xchg/cas/add` + memory order,
  `__builtin_fence` (LLVM `load atomic`/`atomicrmw`/`cmpxchg`/`fence`). Hepsi `__builtin_` önekli, global.
  Memory order, builtin `enum MemoryOrder: ui8 { relaxed, acquire, release, acq_rel, seq_cst }` tipidir
  (2.6); argüman `MemoryOrder.seq_cst` gibi derleme zamanı sabiti olmalıdır. Atomic'lerin kabul ettiği
  tipler: `i8..i64`, `ui8..ui64`, `isize`, `usize`, `*T`, `bool` (diğerleri derleme hatası).
- [ ] **6.2** Fonksiyon pointer'ı: `&fn_name`, dolaylı çağrı, `fn(A) -> R` tipi.
- [ ] **6.3** Generic inline asm builtin'i `__builtin_asm("şablon", in/out/inout/clobber/options...)`
  (**zorunlu**; sözdizimi `PLAN.eg.md` §12.1, LLVM inline asm'ye iner, şablonu yorumlamaz). Şablon hedefe
  özeldir ve `#if arch` / `#if os` ile seçilir (farklı OS için farklı şablon yazılabilir). x86'da
  **Intel** lehçesi, aarch64'te standart sözdizimi. Birden fazla `out` desteklenir (aggregate dönüş yok).
  Varsayılan: bellek okunup yazılabilir sayılır; `options("nomem")` gevşetir, `options("noreturn")` bloğun
  geri dönmediğini bildirir. Gerekçe: ham thread oluşturma (Linux `clone`, macOS `bsdthread_create`,
  Windows `NtCreateThreadEx`) sonrası çocuk thread'in yeni stack'te trampoline'e düşmesi ve scheduler'ın
  bağlam değiştirmesi (stack/register kaydet-yükle).
- [ ] **6.4** Derleme zamanı hedef sabitleri `os`, `arch` (Faz 3'te yerleşik tanım olarak zaten var).
- [ ] **6.5** Builtin tablosu: kapalı tablo (ad, imza, biçim `__builtin_xx` | öneksiz). Sema'da global isim
  olarak çözülür; gövdeyi compiler üretir; çakışma kuralı "Karar bekleyen"e bağlı. Tabloda `__builtin_`
  önekli atomic'ler, `fence` (6.1) ve `__builtin_asm` (6.3) ile `MemoryOrder` enum tipi vardır; `size_of`
  ailesi (2.4) öneksizdir. `syscall` bu tablonun dışındadır (5.3'teki özel işleme). `register` ile
  kaydedilenler bu tabloda değildir (4.12).

## Faz 7 — xlang stdlib (compiler hedefi tamamlandıktan sonra)

İlke: compiler thread/scheduler/print/alloc bilmez; hepsi xlang ile yazılır ve platform
API'sini kendisi çağırır.

```
kullanıcı kodu → std/{thread,sync,sched,io,memory} → std/sys/{linux,macos,windows} (#if os) → syscall
```

- [ ] **7.0** Stdlib modeli: **dış kütüphane yok** (libc, libSystem, kernel32, pthread kütüphanesi kullanılmaz;
  dil dışarıdan kütüphane almaz). Stdlib her OS için o OS'un çekirdek ABI'sini ham `syscall` ile çağırır;
  OS seçimi `#if os == ...` ile yapılır. Compiler yalnızca syscall'ı hedefe çevirir.
  Stdlib **isteğe bağlıdır**, compiler bayraklarıyla yönetilir:

  | Bayrak | Anlam |
  |--------|-------|
  | (varsayılan) | Dile gömülü varsayılan stdlib kullanılır |
  | `--no-std` | Stdlib yok (freestanding); yalnızca dil + global yerleşikler |
  | `--std=<yol>` | Varsayılan yerine başka bir stdlib kök dizini |
  | `--std-features=a,b` | Stdlib özelliklerini aç/kapat (ör. `sched`, `gpu`, `default-allocator`) |
- [ ] **7.1** `std/sys/*`: OS başına ham syscall katmanı, hepsi `#if os == ...` ile seçilir.
  - Linux: `clone/clone3`, `futex`, `mmap/munmap`, `write/read`, `exit`, `epoll_*`.
  - macOS: `bsdthread_create`/`bsdthread_terminate`, `psynch_*` (mutex/cond), `mmap`, `write/read`, `kevent`.
  - Windows: `NtCreateThreadEx`, `NtWaitForSingleObject`/`NtWaitForAlertByThreadId`, `NtAllocateVirtualMemory`,
    `NtWriteFile`/`NtReadFile`; SSN tablosu sürüme göre `-D` ile seçilir.
  - Numara/yapı tabloları xl içinde (compiler görmez). POSIX uyumlu semantik hedeflenir (pthread benzeri
    thread/mutex/cond davranışı), ama pthread kütüphanesi çağrılmaz.
- [ ] **7.1b** Stdlib yerleşimi: `std/index.xl` (giriş; alt modülleri `export * from` / `export * as` ile
  toplar), `std/memory`, `std/mem`, `std/runtime`, `std/assert`, `std/math` (`sqrt` vb.; xl ile yazılır,
  libm yok), `std/cpu`, `std/gpu`, `std/sys/*`. `import "std"` hepsini, `import "std/cpu"` yalnızca ilgili parçayı verir.
- [ ] **7.2** `std/mem`: `interface Allocator { alloc, free }`, varsayılan `Heap` (OS'tan sayfa alan,
  syscall'larla), `register(allocator) global current_allocator: Allocator` (stdlib özelliği
  `default-allocator`), `free(p)` (şimdiki kayıtlı allocator'ın `free`'si), `set_allocator(a)` (bir kez;
  ikinci çağrı `panic`). `std/io`: `print` (compiler'a gömülü değil; global değil, `import` ile gelir).
  `memcpy/memset/memmove/memcmp` `std/memory`'de **xl ile yazılır** ve `export(c)` ile C adıyla verilir
  (LLVM'in örtük çağrıları için; libc yok); global değildir (bkz. kural 7).
- [ ] **7.2b** `std/runtime`: `register(panic) fn panic(msg: *ui8)` (mesajı yazar, süreci sonlandırır);
  stdlib açıkken her zaman derlemeye girer, böylece `panic` her modülde import'suz görünür. `std/assert`:
  `export define assert(cond: expr)` macro'su; gövdesi `panic` çağırır (kayıtlı olduğu için import'suz).
- [ ] **7.3** `std/thread`: `thread_start(entry: fn(*void), arg: *void)`, `thread_join`, `thread_detach`,
  `thread_id`. Doğrudan OS thread'i (7.1'deki ham syscall'lar). Argümanlar heap env'de (global yok →
  veri yarışı yok); yeni thread'in stack'i `mmap`/eşdeğeriyle ayrılır; çocuk girişi 6.3 asm trampoline'i.
- [ ] **7.4** `std/sync`: `__builtin_atomic_*` + OS'un bekleme primitifi (futex / psynch / NtWaitFor...) üstünde
  mutex/cond/rwlock/once. `mutex` vardır (lock/try_lock/unlock; önce atomic CAS, başarısızsa OS bekleme).
  `lock_guard`/RAII bekçi **yoktur**: kilit açık `unlock` ile bırakılır.
- [ ] **7.5** `std/sched`: **Go benzeri scheduler** (M:N): goroutine benzeri hafif görevler, kullanıcı
  alanı stack'leri ve bağlam değiştirme (6.3), her CPU için bir worker thread, iş çalma (work stealing),
  `spawn(fn, arg)`, `wait_all()`, `cpu()`, bloklayıcı I/O için OS olay mekanizması entegrasyonu
  (epoll/kqueue/IOCP benzeri; 7.1 syscall'larıyla). Compiler scheduler'ı bilmez.
- [ ] **7.6** xmake `std` hedefi: stdlib'i derleyip compiler modül arama yoluna kurar.
- [ ] **7.7** Compiler'dan kaldır: `spawn`/`go`, `print` özel işleme, `malloc/free/printf` prelude,
  `needs_heap_`/`needs_strings_`; `new` dilde kalır (4.11, kayıtlı ya da açık allocator), `delete` yoktur.
  Eski example/lib'ler yeni dile göre yeniden yazılır (bu aşamada).

**Kabul:** compiler kaynağında `spawn`, `thread`, `pthread`, `printf`, `malloc` geçmez (link bayrakları
hariç); stdlib hiçbir dış kütüphane çağırmaz (yalnızca `syscall`); Linux ve macOS'ta
(ve mümkünse Windows'ta) aynı kullanıcı kodu farklı `sys/*` ile çalışır; `--no-std` ile stdlib'siz
bir program derlenir.

## Faz 8 — Ölçekleme (ihtiyaç doğunca)

- [ ] **8.1** Modül başına derleme: modül başına LLVM modülü/`.o`, arayüz dosyası, bağımlılık grafiği.
- [ ] **8.2** İçerik hash'li incremental/cache.
- [ ] **8.3** (Koşullu) MIR: yalnızca ikinci backend / dile özgü CFG analizi gerekirse.

---

## Sıra

```
Faz 0 ─► Faz 1 ─► Faz 2 ─► Faz 3 ─► Faz 4 ─► Faz 5 ─► Faz 6 ─► Faz 7 ─► Faz 8
 güvenlik  syntax   tipler    #if      sema/    codegen  primitif  stdlib   ölçek
                    layout    CLI      HIR      kalite
```

- Geriye uyumluluk olmadığı için ara geçiş katmanı yazılmaz; her faz kendi yüzeyini doğrudan değiştirir.
- Faz 5.1–5.3 küçük ve bağımsız; Faz 0'dan sonra herhangi bir noktada öne çekilebilir.
- Faz 2 (layout) ve Faz 4.4–4.5 (class/interface) birbirine bağlı: class düzeni Faz 2 motorunu kullanır.

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
