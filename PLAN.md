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
4. **Compiler yalnızca dili ve CPU primitiflerini bilir.** OS, thread, scheduler, print, malloc
   kavramı compiler'da yoktur; bunlar xlang stdlib'dir.
5. **Önce compiler doğru çalışsın.** Stdlib/example işleri compiler hedefine ulaştıktan sonra gelir.
6. **Plan ve dil şeması senkron kalır:** dil seviyesini etkileyen her plan değişikliği
   `PLAN.eg.md`'yi de günceller (bkz. `CLAUDE.md`).

7. **Global isim uzayı minimal.** Kaynak koduna "bedavaya" gelen isim sayısı çok azdır: yalnızca
   CPU/derleme zamanı gerektiren kapalı bir yerleşik liste (`syscall`, atomic işlemler ve `fence`,
   `size_of/align_of/offset_of`, `static_assert`, `stringify!`; kesin liste Faz 6'da netleşir).
   `memcpy`, `memset`, `print`, `alloc` gibi şeyler **global değildir**; stdlib'den `import` ile gelir.
   xl libc kullanmaz; bunların gövdeleri stdlib'de xl ile yazılır.
   `declare` yalnızca başka bir nesne/kütüphaneden (ör. `api.o`, libc) gelen dış şeyler içindir
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
| Syscall | Builtin `syscall(nr, args...)` (yapıldı) | Compiler hangi syscall olduğunu bilmez |
| Fonksiyon sözdizimi | `fn name(a: i32) -> i32 { }`; `->` yoksa dönüş `void` | İstek: `=>` değil `->` |
| Tamsayı tipleri | `i8 i16 i32 i64 isize` / `ui8 ui16 ui32 ui64 usize` | İşaretli/işaretsiz ayrımı; kullanıcı yazımı `ui32` |
| Kayan nokta | `f32`, `f64` | |
| Diğer tipler | `bool`, `void`, `*T` (pointer), `fn(A) -> R` (fonksiyon pointer'ı), `struct`, `class`, `interface` | |
| Örtük dönüşüm | Sadece aynı işaretlilikte genişleme; diğer her şey açık `as` | Sessiz işaret/daralma hataları olmasın |
| Taşma | İşaretli ve işaretsiz tamsayılar sarar (two's complement), bölme sıfıra = trap | Tanımlı davranış |
| Bellek düzeni | Varsayılan: **xlang düzeni** (derleyici alan sırasını/padding'i seçebilir). `@repr(c)` ile işaretli struct/class **C/C++ düzenine** birebir uyar | İstek |
| Class | Tek kalıtım, sanal metod, vptr; `@repr(c)` ile Itanium C++ ABI düzeni (tek kalıtım) | İstek: compiler class bilsin |
| Interface | Fat pointer `{data, itable}` (Go/Rust tarzı); struct ve class implement edebilir | Düzeni (C uyumu) bozmaz |
| Import | `import "m" as a`, `from "m" import x` **ve** `import x from "m"` (ikisi de geçerli, aynı AST) | İstek: iki sıra da desteklensin |
| Koşullu derleme | Satır tabanlı direktifler `#if/#elif/#else/#endif/#ifdef/#ifndef/#error`; koşul, dilin normal `if` ifadesiyle aynı sözdizimi ve operatörler (`defined(X)`, `!defined(X)`, `&& \|\| !`, karşılaştırma), derleme zamanında basit yorumlayıcıyla değerlendirilir. **Gövde aynı satırdaysa `#endif` gerekmez**, alt satıra inerse `#endif` zorunlu. Pasif bölge yalnızca lex edilir | İstek |
| Dış kütüphane / stdlib | Dil dışarıdan kütüphane almaz: libc, libSystem, kernel32, pthread kütüphanesi yok. Stdlib her OS'un çekirdek ABI'sini `#if os` ile ham `syscall` üzerinden çağırır. Stdlib isteğe bağlı: `--no-std`, `--std=<yol>`; varsayılan stdlib dile gömülü, içinde Go benzeri scheduler var | İstek |
| libc | **Kullanılmaz.** xl'in kendi standart kütüphanesi vardır; `memcpy/memset/memmove/memcmp` stdlib'de xl ile yazılıp `export(c)` ile C adıyla verilir (LLVM'in örtük çağrıları için) | İstek |
| Tanılar | Compiler kısıtlamaz, uyarır: her uyarı `ne + detay + neden + nasıl düzeltilir`; kodlu (`W0201`...), `--allow/--deny/@allow` ile susturulur veya hataya çevrilir, `--explain` | İstek |
| Düzen geçmişi | Varsayılan düzen **sürümlü şema** (v1, v2, v3...), eski şemalar donmuş ve compiler'da kalır; `.o/.a/.bc` düzen kaydı taşır; `xl.layout` proje geçmişi; bilerek karıştırma (`@layout(shuffle)`) kayıtta işaretlenir; karıştırılmamışsa düzen yeniden türetilir | İstek: önceki derleyici çıktılarının düzenini bilme |
| `declare` | Tek önek; her bildirimin başına konur: `declare fn/global/struct/class/interface/type`. Global için ayrı sözdizimi yok. Öznitelikler normal bildirimle aynı (`@repr(c)`, `@packed`, `@align`, `@mangle`); `@repr(c)` `declare`'de **zorunlu değil**; düzen yoksa kayıt → geçmiş → C varsayımı + uyarı. Dış tarafın xl/C++/Rust/`.o`/statik olması kısıtlanmaz | İstek |
| Modül yolları | `"std"` → `std.xl` ya da `std/index.xl`; `"std/memory"` aynı kuralla. Giriş dosyası `index.xl` | İstek: `import "std"` hepsini versin |
| Çift import / çift export | Aynı ada sahip iki bağlama hatadır, **import/export satırında** raporlanır: `import * from a` + `import * from b` (ortak ad), çift adlandırılmış import, yerel tanımla çakışma. `import * as aa from a` + `import * as bb from b` çalışır. Aynı varlığın iki yoldan gelmesi hata değil ama **detaylı uyarı** (`W0401`) + log | İstek |
| Yeniden dışa aktarma | `export * from "m"`, `export * as ns from "m"`, `export { a, b as c } from "m"`, `export * from "m" except a, b`, `#if` içinde koşullu | İstek: JS benzeri ama daha geniş |
| Kaynak uzantısı | `.xl` | İstek |
| Görünürlük | **Varsayılan export yok.** `export` yazılmayan sembole başka modül erişemez; link'te de internal. `export(c)` = `export` + mangle edilmemiş C ABI sembolü | İstek: C-like export, kendi modülleri arası kullanım |
| Name mangling | Varsayılan açık (`xl` şeması). Proje (`--mangle=`), dosya (`@file(mangle = ...)`), fonksiyon (`@mangle(...)`) seviyesinde override; öncelik fn > dosya > proje | İstek |
| Bellek güvenliği | Ham `*T` C gibi kontrolsüz kalır; ek `ref T` = `{ptr, gen}` nesil kontrollü güvenli referans (use-after-free'de trap). Borrow checker / lifetime anotasyonu yok. `--safety=checked\|unchecked`, `@unchecked fn` | İstek: Rust benzeri koruma, dil kalıbını bozmadan |
| C++ birlikte kullanım | Compiler yalnızca `obj` / `ir` (`.ll`) / `bc` / `header` / `exe` / `static` / `shared` üretir; bağlama ve build'i kullanıcının script'i (xmake vb.) yürütür | İstek: compiler karar vermesin |
| Tip takma adı | `type Ad = Tip` (şeffaf alias, yeni tip yaratmaz), `export type` | İstek: typedef benzeri |
| define / macro | `define N = sabit` ve `define f(x: expr) { ... }`, çağrı `f!(...)`; gövde süslü parantezli, `\` yok, `#`/`##` yok, türlü parametre (`expr/type/ident/block/items`), hijyenik; `stringify!`; macro bildirim ve `import` üretebilir, `export define` ile import edilebilir | İstek: C önişlemcisinin pisliği olmadan macro |
| CLI tanımları | `-D/--define NAME[=DEĞER]`, `-U/--undef NAME`, + yerleşik `os`, `arch`, ... | İstek |
| Thread/scheduler/IO/alloc | xlang stdlib; compiler görmez (Faz 7) | Compiler yalnızca düşük seviye bilir |
| Pass manager | New PassManager (`PassBuilder`) | Legacy PM deprecated |
| Derleme modeli | Whole-program, tek LLVM modülü; ayrı derleme en sona | Basit, doğru |
| HIR | Eklenecek | Sema çıktısı; codegen yalnızca tipli ağacı görür |
| LIR | Eklenmeyecek | LLVM IR zaten LIR; ikinci düşük seviye IR kazanç sağlamaz. Koşul: ikinci backend ya da dile özgü CFG analizi → o zaman MIR |

### Karar bekleyen / sonradan netleşecek

- `ui32` yazımı kullanıcı isteğiyle seçildi; `u32`'ye çevirmek yalnızca tip tablosunu değiştirir.
- Sabit boyutlu diziler (`[T; N]`) C uyumlu struct'lar için gerekli; ayrı bir madde olarak eklenecek (Faz 2).
- Generic'ler ve closure kapsam dışı (ilk sürümde yok).

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
  bildirimleri, `declare fn ... -> T`, fonksiyon pointer tipi `fn(A, B) -> R`.
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
  `Float{bits}`, `Pointer`, `FnPtr`, `Struct`, `Class`, `Interface`). Eski `Int32/Int64/BigInt` ayrımı gider.
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
  `ashr/lshr` — tip bazlı seçim. Örtük dönüşüm kuralları sema'da (tek yerde).
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
- [ ] **2.5** Sabit boyutlu dizi tipi `[T; N]` (C struct uyumu için).

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
  - `stringify!(parça)` yerleşik; `#`/`##` ve satır devam karakteri sözdiziminde **yoktur**.
- [ ] **3.8** `type` takma adları sema'da çözülür (Faz 4.1 scope'unda), HIR'da hedef tipe indirgenir;
  döngüsel takma ad hatası.

**Kabul:** `-D DISABLE_X` ile bir item/alan/statement elenir; `--print-cfg` doğru değerleri gösterir.

## Faz 4 — Sema ve HIR

- [ ] **4.1** Sembol tablosu/scope: modül, fonksiyon, blok; nitelikli isim (`alias.name`).
- [ ] **4.2** Sema: isim çözümleme, tip kontrolü, overload yok/var kararı (önerim: overload yok, ad bazlı
  tek tanım, mangling basit), `syscall(...)` argüman doğrulaması (1–7, tamsayı/pointer), `static_assert`.
- [ ] **4.3** HIR (`include/xlang/hir/hir.h`): tipler çözülmüş, örtük dönüşümler açık `Cast`,
  sugar düşürülmüş, sınıf metodları `fn(self: *T, ...)`, interface çağrıları açık itable çağrısı.
- [ ] **4.4** Class:
  - Tek kalıtım, `class B : A`; `virtual` metod, `override`; vptr nesnenin başında.
  - `@repr(c)` class: Itanium düzeni (vptr offset 0; vtable'da offset-to-top ve typeinfo slotları,
    ardından fonksiyonlar). Çoklu/sanal kalıtım ve MSVC ABI kapsam dışı (açık hata).
  - Constructor/destructor: ilk sürümde açık `init`/`deinit` metodları.
- [ ] **4.5** Interface: bildirim, `class/struct implements`, `iface` değeri = `{data: *void, itable: *T}`;
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
  fonksiyon pointer'ı); `class`/`interface`/`ref T` hata.
- [ ] **4.9** Name mangling: tek modül (`src/sema/mangle.cpp`) — `xl` şeması (`_xl$modül$ad$imza`);
  `@file(mangle = ...)`, `@mangle(none|"ad")`, `--mangle=`; öncelik fn > dosya > proje.
  Mangling kapalıyken sembol çakışması derleme hatası.
- [ ] **4.10** `ref T`: `{ptr, gen}` 16 B; allocator `gen` başlığını yönetir (stdlib `std/mem`), compiler
  her erişimde `ref.gen == obj.gen` kontrolü + trap üretir. `&local`'dan ref'in kaçışı derleme hatası
  (basit escape kontrolü, borrow checker değil). `@unchecked fn` ve `--safety=unchecked` kontrolleri kaldırır.
  `ref T` `@repr(c)` alanında ve `export(c)` imzasında yasak.

**Kabul:** class/interface/vtable içeren küçük programlar çalışır; `@repr(c)` class'ın nesne düzeni
ve vtable'ı clang ile birebir uyuşur (tek kalıtım).

## Faz 5 — Codegen kalitesi

- [ ] **5.1** Alloca'lar fonksiyon entry bloğuna (`mem2reg` için).
- [ ] **5.2** `PassBuilder` ile `buildPerModuleDefaultPipeline(level)`; `CodeGenOptLevel` bağla; `--cpu`.
- [ ] **5.3** Syscall hedef OS ayrımı (`src/codegen/target.cpp`): compiler `syscall(nr, args...)`'ı
  hedefin (OS + arch) çağrı kuralına çevirir; hangi syscall olduğunu bilmez.

  | Hedef | Komut | Numara | Argümanlar | Not |
  |-------|-------|--------|------------|-----|
  | Linux x86_64 | `syscall` | `rax` | `rdi rsi rdx r10 r8 r9` | |
  | Linux aarch64 | `svc #0` | `x8` | `x0..x5` | |
  | macOS x86_64 | `syscall` | `rax` (`0x2000000 +` sınıf) | `rdi rsi rdx r10 r8 r9` | BSD sınıfı |
  | macOS aarch64 | `svc #0x80` | `x16` | `x0..x5` | |
  | Windows x86_64 (NT) | `syscall` | `eax` (SSN) | `r10 rdx r8 r9`, fazlası stack (`[rsp+0x28..]`) | 4'ten fazla argümanda compiler stack'i hazırlar |

  Windows NT syscall numaraları (SSN) sürüme göre değişir; numara tablosu stdlib'dedir ve `-D` ile seçilir
  (compiler tabloyu bilmez). Desteklenmeyen OS/arch için açık hata. Compiler'ın "6 argüman" sınırı,
  hedefe göre sınır olur (Windows'ta stack aktarımıyla daha yüksek).
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

- [ ] **6.1** Atomic builtin'ler: `atomic_load/store/xchg/cas/add` + memory order, `fence`
  (LLVM `load atomic`/`atomicrmw`/`cmpxchg`/`fence`).
- [ ] **6.2** Fonksiyon pointer'ı: `&fn_name`, dolaylı çağrı, `fn(A) -> R` tipi.
- [ ] **6.3** Generic inline asm builtin'i (**zorunlu**): ham thread oluşturma (Linux `clone`, macOS
  `bsdthread_create`, Windows `NtCreateThreadEx`) sonrası çocuk thread'in yeni stack'te trampoline'e
  düşmesi ve scheduler'ın bağlam değiştirmesi (stack/register kaydet-yükle) için gerekli.
- [ ] **6.4** Derleme zamanı hedef sabitleri `os`, `arch` (Faz 3'te yerleşik tanım olarak zaten var).

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
  | `--std-features=a,b` | Stdlib özelliklerini aç/kapat (ör. `sched`, `gpu`) |
- [ ] **7.1** `std/sys/*`: OS başına ham syscall katmanı, hepsi `#if os == ...` ile seçilir.
  - Linux: `clone/clone3`, `futex`, `mmap/munmap`, `write/read`, `exit`, `epoll_*`.
  - macOS: `bsdthread_create`/`bsdthread_terminate`, `psynch_*` (mutex/cond), `mmap`, `write/read`, `kevent`.
  - Windows: `NtCreateThreadEx`, `NtWaitForSingleObject`/`NtWaitForAlertByThreadId`, `NtAllocateVirtualMemory`,
    `NtWriteFile`/`NtReadFile`; SSN tablosu sürüme göre `-D` ile seçilir.
  - Numara/yapı tabloları xl içinde (compiler görmez). POSIX uyumlu semantik hedeflenir (pthread benzeri
    thread/mutex/cond davranışı), ama pthread kütüphanesi çağrılmaz.
- [ ] **7.1b** Stdlib yerleşimi: `std/index.xl` (giriş; alt modülleri `export * from` / `export * as` ile
  toplar), `std/memory`, `std/cpu`, `std/gpu`, `std/sys/*`. `import "std"` hepsini, `import "std/cpu"`
  yalnızca ilgili parçayı verir.
- [ ] **7.2** `std/mem`, `std/io`: `alloc/free`, `print` (compiler'a gömülü değil). `memcpy/memset/memmove/memcmp`
  `std/memory`'de **xl ile yazılır** ve `export(c)` ile C adıyla verilir (LLVM örtük çağrıları için, libc yok);
  global değildir (bkz. kural 7).
- [ ] **7.3** `std/thread`: `thread_start(entry: fn(*void), arg: *void)`, `thread_join`, `thread_detach`,
  `thread_id`. Doğrudan OS thread'i (7.1'deki ham syscall'lar). Argümanlar heap env'de (global yok →
  veri yarışı yok); yeni thread'in stack'i `mmap`/eşdeğeriyle ayrılır; çocuk girişi 6.3 asm trampoline'i.
- [ ] **7.4** `std/sync`: atomic + OS'un bekleme primitifi (futex / psynch / NtWaitFor...) üstünde
  mutex/cond/rwlock/once.
- [ ] **7.5** `std/sched`: **Go benzeri scheduler** (M:N): goroutine benzeri hafif görevler, kullanıcı
  alanı stack'leri ve bağlam değiştirme (6.3), her CPU için bir worker thread, iş çalma (work stealing),
  `spawn(fn, arg)`, `wait_all()`, `cpu()`, bloklayıcı I/O için OS olay mekanizması entegrasyonu
  (epoll/kqueue/IOCP benzeri; 7.1 syscall'larıyla). Compiler scheduler'ı bilmez.
- [ ] **7.6** xmake `std` hedefi: stdlib'i derleyip compiler modül arama yoluna kurar.
- [ ] **7.7** Compiler'dan kaldır: `spawn`/`go`, `print` özel işleme, `malloc/free/printf` prelude,
  `needs_heap_`/`needs_strings_`; `new/delete` ya stdlib sembolüne iner ya da kalkar.
  Eski example/lib'ler yeni dile göre yeniden yazılır (bu aşamada).

**Kabul:** compiler kaynağında `spawn`, `thread`, `pthread`, `printf`, `malloc` geçmez (link
bayrakları hariç); stdlib hiçbir dış kütüphane çağırmaz (yalnızca `syscall`); Linux ve macOS'ta
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
5. Layout motoru + `@repr(c)` + `size_of/offset_of/static_assert` + clang karşılaştırma testi (2.3–2.5)
6. `#if` + yerleşik tanımlar (3.1–3.3, 3.5)
7. CLI bayrakları `-D/-U/--target/-O/-g/--emit/--print-cfg` + `define` sabitleri (3.4, 3.6)
7b. Macro genişletme + `type` takma adı (3.7–3.8)
8. Sema + scope + modül yükleme (4.1–4.2, 4.6)
9. HIR + codegen taşıma (4.3, 4.7)
10. Class (4.4), Interface (4.5)
10b. Görünürlük + mangling + `ref T` (4.8–4.10)
11. Alloca/PassBuilder/Darwin syscall/debug info (5.1–5.5)
11b. Çıktı biçimleri + header + static/shared (5.6–5.8)
12. Atomic + fnptr + asm (6)
13. Stdlib (7.1–7.6), ardından compiler'dan kaldırma (7.7)
