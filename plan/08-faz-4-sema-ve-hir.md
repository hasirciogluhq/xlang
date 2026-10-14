## Faz 4 — Sema ve HIR

- [ ] **4.1** Sembol tablosu/scope: modül, fonksiyon, blok; nitelikli isim (`alias.name`).
- [ ] **4.2** Sema: isim çözümleme, tip kontrolü, overload yok/var kararı (önerim: overload yok, ad bazlı
  tek tanım, mangling basit), `syscall(...)` argüman doğrulaması (nr + argümanlar herhangi bir tamsayı tipi ya da `*T`, `as` gerekmez, compiler register genişliğine çevirir; sayı sabit değil, hedef OS/arch'ın
  en fazla argüman sayısına göre 5.3'teki tablodan denetlenir, aşılırsa hata), `staticAssert`.
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
  **Interface nominaldir:** bir tipin interface olarak kullanılabilmesi için `impl Ad for Tip` yazılmış
  olmalıdır; metodların adı/imzası tutsa bile yapısal eşleşme yoktur. `impl` yoksa dönüşüm **hatadır**
  (`E0504`).
- [ ] **4.6a** Yol çözümleme: `"x"` → `x.xl` | `x/index.xl` (ikisi birden hata); yeniden dışa aktarma
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
- [ ] **4.6b** `declare` semantiği: gövdesiz dış öğe, kod üretmez; düzen çözümü sırası: nesne içi düzen kaydı → `xl.layout`
  geçmişi → C varsayımı + `W0201`; `@repr(c)` zorunlu değil; opak tipler yalnızca pointer ile kullanılır; `declare class` metodları dış sembol (`@mangle` ile ad verilir).
- [ ] **4.6** Modül yükleme: AST klonlama yerine modül başına `Program` + sembol tablosu; mangling tek yerde.
- [ ] **4.7** Codegen'i HIR üzerinde sıfırdan yaz (eski AST→LLVM hattı silindi); tip çözümleme sema'dadır,
  codegen yalnızca tipli HIR'ı görür. `verifyModule` (0.1) ilk iş olarak buraya eklenir.

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
- [ ] **4.11** `new T { alanlar }` ve `new(a) T { alanlar }`: dil ifadesi; `a.alloc(sizeOf(T),
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
- [ ] **4.12** `register[(rol)] bildirim`: `declare` gibi tek önek. Bildirilen ad, modül derlemeye girer
  girmez **global** olur (import gerekmez; `register` `export` anlamına da gelir). Kayıtlar modül
  yüklenirken toplanır: import grafiğindeki modüller ve stdlib açıkken (varsayılan; `--no-std` kapatır)
  stdlib kökünün (`index.xl`) yüklediği modüller sayılır; yüklenmeyen modülün kaydı yoktur. Stdlib'in
  yüklenmesi sürücü kararıdır, yüklenen modüller içindeki isimlere compiler özel muamele yapmaz. Roller:
  - `panic`: `fn(msg: *ui8)`; geri dönmez (compiler çağrıdan sonrasını ulaşılamaz sayar). Compiler'ın
    ürettiği hata yolları (2.2 sıfıra bölme, 2.5 dizi sınırı, 4.10 `ref` uyuşmazlığı) onu çağırır; kayıtlı
    yoksa doğrudan trap talimatı üretir.
  - `allocator`: tipi `Allocator` interface'i olan ya da `impl Allocator for T` yazılmış bir `T` olan
    **global değişken** (4.11); `new T` onun `alloc`'unu çağırır.
  Hata kodları: `E0502` (aynı rolün/adın ikinci kaydı), `E0503` (rol sözleşmesi sağlanmadı, ör. allocator
  global'inin tipi `Allocator` değil ve `impl` yok). Aynı adlı
  yerel tanım/import kayıtlı adı gölgeler ve `W0402` uyarısı verir; rol bağı addan değil kayıttan
  olduğundan gölgeleme rolü bozmaz. Compiler `register`'ı modül stdlib'den mi kullanıcıdan mı geliyor diye
  ayırt etmez.

**Kabul:** class/interface/vtable içeren küçük programlar çalışır; `@repr(c)` class'ın nesne düzeni
ve vtable'ı clang ile birebir uyuşur (tek kalıtım).
