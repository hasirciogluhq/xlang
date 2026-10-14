## 19. Tanılar ve uyarılar

Compiler kısıtlamaz, **bilgilendirir**. Kullanıcının bilerek yaptığı şeyi (ör. `declare` ile başka bir
dilden/nesneden gelen bir sembolü bağlamak) engellemez; riskli olabilecek durumlarda uyarı basar ve
uyarılar susturulabilir. Her uyarı dört parça taşır: **ne**, **detay**, **neden**, **nasıl düzeltilir**.

```
warning[W0201]: `Point` için bellek düzeni bilinmiyor, C düzeni varsayıldı
  --> app.xl:12:9
   |
12 | declare struct Point { x: f64, y: f64 }
   |         ^^^^^^^^^^^^ bu tip dış bir nesneden geliyor
   |
detay:  `Point` için bağlanan hiçbir nesnede düzen kaydı yok ve xl.layout içinde de bulunmuyor.
neden:  Dış tarafın derleyicisi/dili bilinmediği için alan sırası ve boşluklar doğrulanamadı.
düzelt: `@repr(c)` ekle (dış taraf C/C++ ise), ya da dış nesneyi xl ile derle (düzen kaydı otomatik gelir),
        ya da bu uyarıyı kasıtlıysa sustur:  @allow(W0201)  veya  --allow=W0201
```

### Hatalı yeri gösterme

Compiler hatalı/uyarılı yeri **kaynak satırının yanına nokta atışı yorumla** gösterir: satırın sonuna
`<=======` ve o noktaya özel açıklama yazılır. Aynı olayın birden fazla yeri varsa her biri kendi satırında
işaretlenir (çakışmalarda iki taraf da). Bir satırda birden fazla nokta varsa altına `^^^` ile tam token
vurgulanır.

```
error[E0401]: `lorem` çift import
  --> app.xl
   |
 3 | import * from "a"      <======= `lorem` burada a.xl:12'den geliyor
 4 | import * from "b"      <======= ve burada b.xl:30'dan da geliyor: çakışma
   |
detay:  İki farklı varlık aynı ada bağlanmaya çalışıyor.
neden:  `lorem` a.xl ve b.xl'de ayrı ayrı tanımlı; hangisinin kullanılacağı belirsiz.
düzelt: `import * as aa from "a"` / `import * as bb from "b"` ya da `import lorem as loremB from "b"`.
```

```
warning[W0201]: `Point` için düzen bilinmiyor, C düzeni varsayıldı
  --> app.xl
   |
12 | declare struct Point { x: f64, y: f64 }    <======= bu tip dış nesneden geliyor
   |                 ^^^^^
```

### Bağlam satırları ve satır numaraları

İşaretlenen noktadan yukarı ve aşağı kaç satırın gösterileceği **önem derecesine göre** değişir:
hatada çok, uyarıda az.

| Tür | Varsayılan bağlam | Neden |
|-----|-------------------|-------|
| `error` | noktadan **−10 / +10** satır | Hatanın çevresini anlamak için geniş görünüm |
| `warning` | noktadan **−2 / +2** satır | Dikey yer kaplamasın |

| Bayrak | Anlam |
|--------|-------|
| `--context-errors=N` | Hatalarda üst/alt satır sayısı (varsayılan 10) |
| `--context-warnings=N` | Uyarılarda üst/alt satır sayısı (varsayılan 2) |
| `--context=N` | İkisini birden ayarlar |

Satır numaraları ve blok biçimi:

- Numara sütunu, gösterilen en büyük numaraya göre **sağa hizalanır** (ör. 8 ve 12 aynı genişlikte).
- İşaretli satırlar `<=======` yorumunu taşır; bağlam satırları soluk (yorumsuz) gösterilir.
- Birbirine yakın birden fazla nokta (çakışmanın iki tarafı) **tek blokta birleştirilir**, bağlam
  örtüşüyorsa satırlar tekrarlanmaz.
- Birleştirilemeyen uzak noktalar arasındaki boşluk `...` satırı ile gösterilir, numara atlaması görünür kalır.
- Dosya başı/sonu sınırında bağlam kırpılır; boş satırlar da sayılır.
- Sekmeler boşluğa açılır, çok uzun satırlar `…` ile kısaltılır ama işaretli sütun korunur.

Uyarı (±2):

```
warning[W0401]: `lorem` aynı varlık olarak iki yoldan import edildi
  --> app.xl
    |
  3 | import "std" as std
  4 |
  5 | import * from "a"      <======= `lorem` burada a üzerinden geliyor (asıl tanım: c.xl:7)
  6 | import * from "b"      <======= ve burada b üzerinden de geliyor (aynı tanım: c.xl:7)
  7 |
  8 | fn main() -> i32 {
```

Hata (±10, kısaltılmış gösterim; gerçekte noktanın üstünde ve altında 10'ar satır):

```
error[E0401]: `lorem` çift import
  --> app.xl
    |
  1 | // app.xl
  2 | import "std" as std
  ⋮   (üstte 10 satıra kadar bağlam)
 11 | import * from "a"      <======= `lorem` burada a.xl:12'den geliyor
 12 | import * from "b"      <======= ve burada b.xl:30'dan da geliyor: çakışma
 13 |
 14 | fn main() -> i32 {
  ⋮   (altta 10 satıra kadar bağlam)
 22 |     return 0
    |
detay:  İki farklı varlık aynı ada bağlanmaya çalışıyor.
```

### Kayıt (log)

Tüm tanılar, ekrana basılanın yanında isteğe bağlı olarak dosyaya da yazılır.

| Bayrak | Anlam |
|--------|-------|
| `--diag-log=<dosya>` | Tüm tanıları (hata, uyarı, susturulanlar dahil) dosyaya yaz |
| `--diag-format=text\|json` | Ekran/log biçimi (`json`: satır başına bir tanı, araçlar için) |
| `--diag-log-level=all\|warn\|error` | Log'a hangi seviyeler yazılsın |

Susturulan (`@allow`/`--allow`) uyarılar ekrandan düşer ama `--diag-log` açıksa "susturuldu" işaretiyle log'a yazılır.

| Susturma | Kapsam |
|----------|--------|
| `--allow=W0201` / `-Wno-W0201` | Tüm derleme |
| `@allow(W0201)` | Statement'ın, bloğun, bildirimin veya fonksiyonun üstünde; o kapsam |
| `@file(allow = W0201)` | Dosyanın en başında; tüm dosya (`@file(mangle = ...)` ile aynı biçim) |
| `--deny=W0201` | Uyarıyı hataya çevirir |
| `--warn-all` / `--allow-all` | Hepsini aç / hepsini sustur |
| `--explain W0201` | Uyarının uzun açıklamasını yazdırır |

> Gerçek çıktıda etiketler İngilizcedir (`detail:` / `reason:` / `fix:`), ok satırı `--> dosya:satır:sütun`,
> işaretli satırlar `<=======` ile biter; yukarıdaki `detay/neden/düzelt` yalnızca şema gösterimidir.
> Çıktı kaynak konumuna göre sıralıdır.

Örnek uyarılar:

| Kod | Ne zaman | Varsayılan |
|-----|----------|-----------|
| `E0001` | Sözcüksel hata: bilinmeyen karakter, kapanmamış string, bozuk sayı/kaçış, satır başında olmayan `#` | **hata** |
| `E0002` | Sözdizimi hatası: beklenen token yok, deyim sonu yok, geçersiz atama hedefi | **hata** |
| `E0003` | Önişleme: `#error` dalı seçildi, bozuk/kapanmayan direktif, koşulda tanımsız ad | **hata** |
| `E0004` | Macro: yanlış argüman sayısı/türü, çift tanım, derinlik > 64 (genişleme zinciri etiketlerde) | **hata** |
| `W0201` | `declare` tipin düzeni bilinmiyor, C varsayıldı | uyarı |
| `W0202` | `declare` tipin düzeni, kayıtlı düzenle çelişiyor | uyarı |
| `W0203` | Düzen kaydı farklı bir düzen şemasıyla üretilmiş (`v2` ↔ `v3`) | uyarı |
| `W0301` | `declare` edilen sembol bu derlemede çözülemedi (link zamanında aranacak) | uyarı |
| `E0302` | `export(c)` imzası C'de ifade edilemeyen bir tip içeriyor | **hata** |
| `W0401` | Aynı varlık iki farklı yoldan import/export edildi | uyarı |
| `E0401` | Farklı varlıklar aynı ada import/export ediliyor | **hata** |
| `E0501` | `new T { }` için kayıtlı allocator yok (`register(allocator)` bulunamadı) | **hata** |
| `E0502` | Aynı rol ya da aynı global ad iki kez `register` edilmiş | **hata** |
| `E0503` | `register` rolünün sözleşmesi sağlanmamış (ör. `allocator` global'inin tipi `Allocator` değil ve `impl` yok) | **hata** |
| `E0504` | Tip bir interface olarak kullanılıyor ama `impl Interface for Tip` yok (yapısal eşleşme sayılmaz; `new(a)`'da `a` dahil) | **hata** |
| `W0402` | Yerel tanım ya da import, `register`'lı bir global adı gölgeliyor | uyarı |
| `E0601` | İşaretlilik değiştiren örtük dönüşüm (`ui ↔ i`, ya da işaretlilikleri karışık ikili işlem); `unsafe { }` içinde ya da `as` ile geçer ([2.1](02.1-unsafe.md#21-unsafe-bloğu)) | **hata** |
| `E0701` | Dizi indeksi sabit ve sınır dışı (`a[7]`, `a: [T; 4]`); `--safety`'den bağımsız | **hata** |
| `E0702` | `staticAssert` koşulu yanlış (ya da bool sabit değil) | **hata** |
| `E0703` | Boyutu bilinmeyen tip: `void`, opak `declare`, değerle özyineleme, yüklenmemiş modül tipi | **hata** |
| `E0704` | Döngüsel `type` takma adı | **hata** |
| `E0705` | Geçersiz enum (alt tip, çift üye, eksik/yanlış değer) ya da `@align`/`@layout` argümanı | **hata** |
| `E0706` | Bilinmeyen tip/ad | **hata** |
| `E0707` | Derleme zamanı sabiti olmayan ifade (dizi uzunluğu, enum değeri, `staticAssert`, `sizeOf`) | **hata** |
| `E0708` | Tip uyuşmazlığı: örtük dönüşümü olmayan atama/argüman/dönüş (`as` gerekir) | **hata** |
| `E0709` | Geçersiz işlem: bilinmeyen alan/metod, atanamayan ya da adresi alınamayan ifade, işaretçi aritmetiği | **hata** |
| `E0710` | Yanlış argüman sayısı; `syscall` hedefin sınırını aşıyor | **hata** |
| `E0711` | Modül bulunamadı, `x.xl` ile `x/index.xl` birlikte var, ya da modül o adı export etmiyor | **hata** |
| `E0712` | `virtual`/`override` kuralı: hiçbir şeyi geçersiz kılmayan `override`, imza farkı, gizleme | **hata** |
| `E0713` | `impl` interface ile uyuşmuyor: eksik/fazla metod ya da imza farkı | **hata** |
| `E0714` | Kod üretimi hatası (çözülmemiş dış başvuru, desteklenmeyen hedef, doğrulama) | **hata** |
| `W0601` | İşaretli tamsayı → `f32/f64` örtük dönüşüm, duyarlılık kaybolabilir ([2](02-tip-donusumu.md#i--f-uyarısı)) | uyarı |
