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
