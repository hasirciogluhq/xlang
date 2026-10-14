### Karar bekleyen / sonradan netleşecek

- `ui32` yazımı kullanıcı isteğiyle seçildi; `u32`'ye çevirmek yalnızca tip tablosunu değiştirir.
- Generic'ler ve closure kapsam dışı (ilk sürümde yok).
- **Önek kuralı:** atomic'ler, `fence` ve `__builtin_asm` `__builtin_` önekli; `syscall`, `sizeOf` ailesi,
  `staticAssert` öneksiz. İleride eklenecek builtin için ölçüt yazılmalı, yoksa her ekleme ayrı karar olur.
- **Liste sınırı:** builtin listesi kapalıdır (kural 7); eklenecekler "compiler gövdeyi doğrudan
  sağlayabiliyor mu" ölçütüne tabidir.
- **Builtin adı çakışması:** kullanıcı/stdlib bir builtin adını (ör. `sizeOf`) tanımlarsa hata mı,
  gölgeleme mi? (`register` adları için gölgeleme `W0402` ile karara bağlandı.)
- **Güvenli referans (`ref T`):** plandan çıkarıldı (nesil kontrollü `{ptr, gen}` fikri). İleride istenirse
  ayrı bir özellik olarak sıfırdan tasarlanır; şu an dilde ve planda yoktur.
- **String literal:** `panic`/`print` string'in `*ui8` olduğunu ve sonunun 0 ile bittiğini varsayıyor;
  string literal'in dil seviyesinde kuralı yazılmadı, uzunluk taşıyan ayrı bir `str` tipi planlanmadı.
- **`setAllocator` sınırı:** ilk `new`'den sonra çağrılırsa önceki bloklar yeni allocator'a bırakılabilir;
  bu tespit edilmez. Kural: `main`'in başında çağrılır (belgelendi); ileride tespit eklenebilir.
- **`panic` imzası:** dosya/satır konumu iletilmiyor; ileride `register(panic)`'e ek parametre
  eklenebilir. `msg` şu an sabit NUL'lu string.
- **Dizi ayrıntıları:** uzunluk için ayrı builtin yok (`sizeOf(a) / sizeOf(T)`); dilim (slice) tipi yok;
  sabit indeksin sınır dışı olması derleme hatasıdır (`E0701`, karar verildi: 2.5).
- **Enum ayrıntıları:** string enum'da değerden üyeye ters arama yok; `export(c)` imzasında yalnızca tamsayı
  alt tipli enum geçerli sayıldı; enum'un `@repr(c)` düzeni alt tipin düzenidir.
- **`register` ayrıntıları:** `define` (macro) kaydı kapsam dışı; `register` ile kaydedilen sembolün
  başka bir kaydı gölgelemesi/çakışması `E0502`; rol sayısı (şu an `panic`, `allocator`) yeni ihtiyaçta
  genişler, her yeni rol bu listeye ve 4.12'ye yazılmalı.
- **`unsafe` ayrıntıları:** yalnızca `unsafe { }` bloğu var; `unsafe fn` ya da bildirim düzeyi işaret yok
  (blok başına yazmak ağır gelirse eklenir). `unsafe` şu an tek kuralı gevşetir (`ui ↔ i` örtük, iki yön aynı);
  `*T` dönüşümleri, `@unchecked fn` ve `--safety` ile ilişkisi bilerek ayrı tutuldu. `W0601` yalnızca
  **işaretli** tamsayı → float için (`unsafe` içinde `ui → f` geçerken de verilir). Float işaretli sayılır,
  bu yüzden `ui → f` `E0601`'dir. `f32 → f64` genişlemesi bu kuralın dışındadır (örtük, uyarısız).
- **Linker/arşivleyici seçimi:** `exe`/`shared`/`static` için compiler'ın hangi aracı çağıracağı belirlenmedi
  (LLVM'e gömülü `lld` mi, hedefin sistem linker'ı mı, `ar` yerine kendi arşiv yazıcısı mı); araç yolunu
  veren bayrak (`--linker=<yol>`) gerekip gerekmediği, hedef başına varsayılan ve çapraz derlemede nasıl
  seçileceği de açık. Her durumda libc/libSystem/kernel32 verilmez (5.9).
- **İsimlendirme ayrıntıları:** tipler `PascalCase`, fonksiyon/metod/değişken/alan/builtin `camelCase`
  (karar verildi). Henüz yazılmayanlar: `define` sabitleri (`MAX_CLIENTS`, `ENABLE_STATS` örneklerde
  SCREAMING_SNAKE kaldı), enum üyeleri (kullanıcı enum'ları `First`, builtin `MemoryOrder` üyeleri
  `relaxed/seqCst`), modül/dosya adları (`sys_linux` gibi) ve stdlib özellik adları (`default-allocator`,
  kebab) için kural.
- **Operatör kümesi:** dil yüzeyinde (örneklerde) yalnızca `+ - * / %`, karşılaştırmalar, `&& || !`, `&`/`*`
  (adres/deref) ve `as` var. Bit operatörleri (`& | ^ ~ << >>`), bileşik atama (`+=`...), `break`/`continue` ve
  `for` yazılmadı; stdlib (atomic bayrakları, bit işlemleri) bunlara ihtiyaç duyacak. Eklenip eklenmeyeceği
  ve sözdizimi kararı bekliyor (lexer/parser şu an tanımıyor).
- **`import "m"` (takma adsız):** 1.2b yalnızca `import "m" as a`'yı listeler, 13 ise `import "std"` yazıyor.
  Parser takma adsız biçimi kabul eder (alias boş); sema'nın bunu `std.` öneki mi yoksa `import *` mı sayacağı
  belirlenmedi.
- **Bildirim kodlarının sınırları:** `E0001` (sözcüksel) ve `E0002` (sözdizimi) yalnızca "genel sözdizimi hatası"
  için açıldı; ayrı kodlara bölme ihtiyacı doğarsa kayıt tablosunda yapılır.
- **`syscall` argüman sınırı (Windows):** 5.3 tablosu Linux/macOS (x86_64, aarch64) için en fazla 6 argüman
  verir; Windows NT için sınır yazılı değil (4'ten fazlası stack'e taşar). Sema şu an Windows'ta sınır
  **uygulamıyor** (`E0710` yalnızca tablodaki hedeflerde). Sayısal bir üst sınır istenirse tabloya yazılmalı.
- **`nil`:** builtin sabittir, anahtar sözcük değildir (lexer'da `null` yok); sema `nil` adını çözer, yerel
  tanımla gölgelenebilir.
- **Sabit katlama:** HIR'da `N * 2` gibi sabit ifadeler katlanmaz (global başlatıcıda sabit sayılır, katlama
  codegen'e/ileride bir sadeleştirmeye kalır).
- **Bildirilmeyen dönüş yolu:** değer döndüren fonksiyonun sonuna "düşmesi" için akış analizi yapılmıyor
  (planda yok); gerekirse ayrı karar.
- **macOS ham thread oluşturma:** `bsdthread_register` (366) libSystem'siz bir süreçte `EINVAL` dönüyor, bu yüzden
  `bsdthread_create` denenemedi. Seçenekler: register çağrısının doğru imzasını/önkoşulunu bulmak, mach
  `thread_create_running` (MIG mesajı) ya da macOS'ta scheduler'ı tek thread'de bırakmak (şu anki davranış).
- **Windows çıkış noktası ve syscall:** çıkış `NtTerminateProcess` SSN'sine ihtiyaç duyar (derleyici bilmez):
  `mainCRTStartup` şimdilik `main`'i çağırıp `int3` ile durur; stdlib'in kendi çıkışını yapması beklenir.
  4'ten fazla argümanlı `syscall` (stack aktarımı) henüz hata verir. Windows stdlib kodu doğrulanmadı.
- **Itanium typeinfo:** `@repr(c)` class vtable'ında typeinfo slotu `null`; C++ RTTI/`dynamic_cast` ile
  birlikte çalışma için `_ZTI...` sembolü üretilmedi.
- **`export(c)` ve aggregate değerler:** `export(c)` fonksiyonlarda struct'ın değerle geçişi/dönüşü şimdilik
  xl çağrı kuralıdır (işaretçiyle); hedef C ABI'sinin (SysV/AAPCS) küçük struct kayıt geçişi uygulanmadı.
  Yalnızca işaretçi, skaler ve `@repr(c)` struct imzaları C tarafıyla doğrulanmalıdır.
- **Modül çözümleme stratejisi:** şu an: `std`/`std/x` → stdlib kökü; diğerleri içe aktaran dosyanın dizini,
  sonra kök dosyanın dizini. `import "x"` takma adsız biçimi son yol parçasını ad alanı yapar (`std/mem` → `mem`).
  Paket/arama yolları henüz yok.
- **Gömülü stdlib:** "dile gömülü varsayılan stdlib" şimdilik derleyicinin yanındaki `std/` dizinidir
  (`xmake install` `share/xlang/std`'e kurar); tek dosyalı gömme yapılmadı.
- **Bit operatörleri ve `break`/`continue`:** stdlib bunlar olmadan yazıldı (bayraklar önceden hesaplanmış
  sabitler, `%`/`/` ile bit çıkarma). Dile eklenirse stdlib sadeleşir.
