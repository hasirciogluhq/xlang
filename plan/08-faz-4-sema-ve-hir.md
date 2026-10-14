## Faz 4 — Sema ve HIR

- [x] **4.1** Sembol tablosu/scope: modül, fonksiyon, blok; nitelikli isim (`alias.name`).
- [x] **4.2** Sema: isim çözümleme, tip kontrolü, overload yok/var kararı (önerim: overload yok, ad bazlı
  tek tanım, mangling basit), `syscall(...)` argüman doğrulaması (nr + argümanlar herhangi bir tamsayı tipi ya da `*T`, `as` gerekmez, compiler register genişliğine çevirir; sayı sabit değil, hedef OS/arch'ın
  en fazla argüman sayısına göre 5.3'teki tablodan denetlenir, aşılırsa hata), `staticAssert`.
- [x] **4.3** HIR (`include/xlang/hir/hir.h`): tipler çözülmüş, örtük dönüşümler açık `Cast`,
  sugar düşürülmüş, sınıf metodları `fn(self: *T, ...)`, interface çağrıları açık itable çağrısı.
- [x] **4.4** Class:
  - Tek kalıtım, `class B : A`; `virtual` metod, `override`; vptr nesnenin başında.
  - `@repr(c)` class: Itanium düzeni (vptr offset 0; vtable'da offset-to-top ve typeinfo slotları,
    ardından fonksiyonlar). Çoklu/sanal kalıtım ve MSVC ABI kapsam dışı (açık hata).
  - Constructor/destructor: ilk sürümde açık `init`/`deinit` metodları.
- [x] **4.5** Interface: bildirim, `impl Ad for Tip { ... }` (struct ve class implement edebilir),
  interface değeri = `{data: *void, itable: *T}`;
  statik tipten arayüze dönüşüm itable'ı derleme zamanında üretir; çağrı = itable'dan dolaylı çağrı.
  **Interface nominaldir:** bir tipin interface olarak kullanılabilmesi için `impl Ad for Tip` yazılmış
  olmalıdır; metodların adı/imzası tutsa bile yapısal eşleşme yoktur. `impl` yoksa dönüşüm **hatadır**
  (`E0504`).
- [x] **4.6a** Yol çözümleme: `"x"` → `x.xl` | `x/index.xl` (ikisi birden hata); yeniden dışa aktarma
  grafiği tek geçişte çözülür; çakışma **her zaman hatadır** (`E0401`) ve `import`/`export` satırında
  raporlanır, kullanım yerinde değil; yerel tanım kazanmaz; export edilmeyen
  sembol yeniden dışa aktarılamaz (4.8 ile uyumlu); `export(c)` aktarmada korunur.
  **Çift import da aynı kuralla hatadır** (`import * from a` + `import * from b` ortak ad → import satırında
  hata; `import * as aa` / `import * as bb` serbest; aynı varlık iki yoldan gelirse hata değil, `W0401` uyarısı + log; farklı varlıklar `E0401` hatası).
  **Aynı ada sahip iki export hatadır** (aynı dosyada çift export, farklı dosyalardan aynı ad, `export *`
  ile gelen ve yerel olan dahil); "yerel kazanır" kuralı yoktur, çakışma zorla hatadır ve çakışan ad hiç
  kullanılmasa bile verilir. Çözüm yolları: `except`, `as`, `export * as ns`.
  Modül çözümleme stratejileri (birden fazla yol: göreli, proje kökü, arama yolları, std, paket) **henüz
  planlanmadı**; bu madde yalnızca dosya/dizin kuralını (`x.xl` | `x/index.xl`) kapsar.
- [x] **4.6b** `declare` semantiği: gövdesiz dış öğe, kod üretmez; düzen çözümü sırası: nesne içi düzen kaydı → `xl.layout`
  geçmişi → C varsayımı + `W0201`; `@repr(c)` zorunlu değil; opak tipler yalnızca pointer ile kullanılır; `declare class` metodları dış sembol (`@mangle` ile ad verilir). Bu derlemede (ve verilen nesnelerde) çözülemeyen `declare` sembolü `W0301` uyarısıdır (hata değil): link zamanında aranır, `--emit=obj`'da normaldir.
- [x] **4.6** Modül yükleme: AST klonlama yerine modül başına `Program` + sembol tablosu; mangling tek yerde.
- [x] **4.7** Codegen'i HIR üzerinde sıfırdan yaz (eski AST→LLVM hattı silindi); tip çözümleme sema'dadır,
  codegen yalnızca tipli HIR'ı görür. `verifyModule` (0.1) ilk iş olarak buraya eklenir.

- [x] **4.8** Görünürlük: `export` olmayan hiçbir sembole (fonksiyon, tip, global, `define`, `type`)
  başka modülden erişilemez (sema hatası); LLVM linkage'ı `InternalLinkage`. `export` →
  `ExternalLinkage` (xl mangling'li); `export(c)` → `ExternalLinkage`, C ABI adı.
  `export(c)` imzasında yalnızca C uyumlu tipler (tamsayılar, `f32/f64`, `bool`, `*T`, `@repr(c)` struct,
  fonksiyon pointer'ı, tamsayı alt tipli enum); `class`/`interface`/string enum hata (`E0302`).
- [x] **4.9** Name mangling: tek modül (`src/sema/mangle.cpp`) — `xl` şeması (`_xl$modül$ad$imza`);
  `@file(mangle = ...)`, `@mangle(none|"ad")`, `--mangle=`; öncelik fn > dosya > proje.
  Mangling kapalıyken sembol çakışması derleme hatası.
- [x] **4.11** `new T { alanlar }` ve `new(a) T { alanlar }`: dil ifadesi; `a.alloc(sizeOf(T),
  alignOf(T))` çağırır, alanları yazar ve `*T` döndürür. Allocator, `std/mem`'in **`Allocator` interface'i**dir
  (`alloc(self, size: usize, align: usize) -> *void` ve `free(self, p: *void)`); `a` ya `Allocator` değeridir
  ya da `impl Allocator for T` yazılmış bir `T`'nin değeri/pointer'ıdır (4.5'teki örtük interface
  dönüşümü). `impl` yoksa hata (`E0504`); metodlar tutsa bile yapısal eşleşme yoktur. Bellek ayıran ve
  bırakan hep aynı allocator'dır. `new T` **kayıtlı allocator**'ı kullanır: `register(allocator)`
  ile kaydedilmiş global (4.12). Compiler adı değil kaydı arar, `malloc` bilmez (kural 4) ve stdlib'i
  ayırt etmez: öncelik kuralı yoktur. Programda toplamda **en fazla bir** `register(allocator)` olabilir
  (iki tane `E0502`). Stdlib `std/mem`'de bir tane kaydeder (stdlib özelliği `default-allocator`,
  varsayılan açık); kullanıcı kendi allocator'ını `--std-features=-default-allocator` ile özelliği kapatıp
  kaydederek ya da çalışma zamanında `main`'de `std/mem.setAllocator(a)` ile **bir kez** değiştirerek
  seçer; kayıtlı global mutable olduğundan `new` ve `std/mem.free` değişimden sonra yeni allocator'ı
  kullanır. Kayıtlı yoksa `new T` hatadır (`E0501`, düzelt: `new(a) T` ya da `register(allocator)`).
  `alloc` `nil` dönerse `new` `nil` döndürür.
- [x] **4.12** `register[(rol)] bildirim`: `declare` gibi tek önek. Bildirilen ad, modül derlemeye girer
  girmez **global** olur (import gerekmez; `register` `export` anlamına da gelir). Kayıtlar modül
  yüklenirken toplanır: import grafiğindeki modüller ve stdlib açıkken (varsayılan; `--no-std` kapatır)
  stdlib kökünün (`index.xl`) yüklediği modüller sayılır; yüklenmeyen modülün kaydı yoktur. Stdlib'in
  yüklenmesi sürücü kararıdır, yüklenen modüller içindeki isimlere compiler özel muamele yapmaz. Roller:
  - `panic`: `fn(msg: *ui8)`; geri dönmez (compiler çağrıdan sonrasını ulaşılamaz sayar). Compiler'ın
    ürettiği hata yolları (2.2 sıfıra bölme, 2.5 dizi sınırı) onu çağırır; kayıtlı
    yoksa doğrudan trap talimatı üretir.
  - `allocator`: tipi `Allocator` interface'i olan ya da `impl Allocator for T` yazılmış bir `T` olan
    **global değişken** (4.11); `new T` onun `alloc`'unu çağırır.
  Hata kodları: `E0502` (aynı rolün/adın ikinci kaydı), `E0503` (rol sözleşmesi sağlanmadı, ör. allocator
  global'inin tipi `Allocator` değil ve `impl` yok). Aynı adlı
  yerel tanım/import kayıtlı adı gölgeler ve `W0402` uyarısı verir; rol bağı addan değil kayıttan
  olduğundan gölgeleme rolü bozmaz. Compiler `register`'ı modül stdlib'den mi kullanıcıdan mı geliyor diye
  ayırt etmez.

> **Durum notu (4.1–4.3):** `include/xlang/sema/scope.h` (`ScopeStack`: modül → fonksiyon → blok, iç kapsam
> dış kapsamı gölgeler, aynı kapsamda ikinci bağlama `E0401`), `src/sema/lower.cpp` (gövde tip denetimi ve HIR
> üretimi **tek geçiş**: `Sema::lower()`) ve `include/xlang/hir/hir.h` yazıldı; `xlang check <dosya> --hir` tipli
> HIR'ı basar. Hata kodları: `E0708` tip uyuşmazlığı, `E0709` geçersiz işlem (bilinmeyen alan/metod, adres
> alınamaz, atanamaz...), `E0710` argüman sayısı / `syscall` sınırı; `E0601`, `W0601`, `E0701`, `E0501`–`E0504`,
> `W0402` ilgili yerlerde kullanılır. Kapsam dışı bırakılanlar: modül yükleme (4.6: `import` ile gelen adlar
> `External` düğümü ve bilinmeyen tip olur, hata verilmez), vtable slot numaraları (`CallVirtual` yalnızca
> `Sınıf.metod` taşır; 4.4), `impl` metod kümesinin interface ile doğrulanması (4.5), `__builtin_*` ve inline asm
> (Faz 6; şimdilik "desteklenmiyor" hatası), `W0201`/`W0301`/`E0302`/`E0401` modül düzeyi kontrolleri (4.6b, 4.8).
> Düzen dönüşü: HIR dizi tipleri sabit uzunluğu literal olarak taşır.

**Kabul:** class/interface/vtable içeren küçük programlar çalışır; `@repr(c)` class'ın nesne düzeni
ve vtable'ı clang ile birebir uyuşur (tek kalıtım).

> **Durum notu (4.4–4.12):** `Compilation` (`include/xlang/sema/compilation.h`) modülleri yükler: iki geçişli
> ayrıştırma (önce içe aktarılan modüllerin `export define` macro'ları tohumlanır), export grafiği tek geçişte
> çözülür (`export *`, `export * as`, `export { }`, `except`; döngü güvenli), importlar `E0401`/`E0711`/`W0401`
> kurallarıyla bağlanır, `register`'lı adlar tüm modüllerde global olur. Çözülmüş tip yolları `modül::Ad`
> (kök modül için yalın `Ad`) biçimindedir; fonksiyon/global/aggregate adları HIR'da böyle nitelenir.
> Class: vtable slot'ları taban slotları önce olacak şekilde atanır, `override`/`virtual` kuralları `E0712`;
> `@repr(c)` class'ın vtable'ı `[offset-to-top, typeinfo, işlevler]` (typeinfo `null`, bkz. 03).
> Interface: itable = interface metod sırası; `impl` uyuşmazlığı `E0713`. Sembol adları 4.9'a göre
> `_xl$modül$ad$imza` (`@mangle`, `@file(mangle)`, `--mangle`, `export(c)` önceliğiyle); mangling kapalıyken
> çakışma `E0401`. `export(c)` imzasında C dışı tip `E0302`. Çözülemeyen `declare` sembolü `W0301`.
