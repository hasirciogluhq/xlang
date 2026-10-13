### Karar bekleyen / sonradan netleşecek

- `ui32` yazımı kullanıcı isteğiyle seçildi; `u32`'ye çevirmek yalnızca tip tablosunu değiştirir.
- Generic'ler ve closure kapsam dışı (ilk sürümde yok).
- **Önek kuralı:** atomic'ler, `fence` ve `__builtin_asm` `__builtin_` önekli; `syscall`, `sizeOf` ailesi,
  `staticAssert` öneksiz. İleride eklenecek builtin için ölçüt yazılmalı, yoksa her ekleme ayrı karar olur.
- **Liste sınırı:** builtin listesi kapalıdır (kural 7); eklenecekler "compiler gövdeyi doğrudan
  sağlayabiliyor mu" ölçütüne tabidir.
- **Builtin adı çakışması:** kullanıcı/stdlib bir builtin adını (ör. `sizeOf`) tanımlarsa hata mı,
  gölgeleme mi? (`register` adları için gölgeleme `W0402` ile karara bağlandı.)
- **`new` ve `ref T`:** `new T{}` `*T` döndürür; `ref T`'ye atamak için `gen` başlığı (4.10) gerekir:
  kayıtlı allocator'ın ref-uyumlu olması mı, ayrı bir yazım mı belirlenmedi (§17 örneği `new Node{}`'u
  `ref Node`'a atıyor).
- **`syscall` argüman tipleri:** pointer'ın `as` olmadan geçip geçmeyeceği (5.5 "syscall sınırında" der)
  örneklerde tutarsız yazıldı; tek kural yazılmalı.
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
- **Dosya kapsamlı `@allow`:** `@allow(KOD)` dosyanın en üstüne yazılırsa ilk bildirime bağlanır, parser dosya
  kapsamı ile ilk bildirim kapsamını ayıramaz. `@file(...)` biçimindeki özniteliğe taşınması (`@file(allow = W0201)`)
  ya da ayrı bir söz dizimi seçilmeli.
- **`import "m"` (takma adsız):** 1.2b yalnızca `import "m" as a`'yı listeler, 13 ise `import "std"` yazıyor.
  Parser takma adsız biçimi kabul eder (alias boş); sema'nın bunu `std.` öneki mi yoksa `import *` mı sayacağı
  belirlenmedi.
- **Bildirim kodlarının sınırları:** `E0001` (sözcüksel) ve `E0002` (sözdizimi) yalnızca "genel sözdizimi hatası"
  için açıldı; ayrı kodlara bölme ihtiyacı doğarsa kayıt tablosunda yapılır.
