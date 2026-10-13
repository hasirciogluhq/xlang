## 2. Tip dönüşümü

**Örtük dönüşüm**, `as` yazmadan compiler'ın dönüşümü kendisinin yapmasıdır (`local x: i64 = small`).
**Açık dönüşüm** `as` ile elle yazılır ve her yerde geçer.

Örtük dönüşüm iki grupla sınırlıdır: **aynı işaretlilikte tamsayı genişlemesi** ve **işaretli tamsayı →
kayan nokta** (uyarıyla). İşaretlilik değiştiren örtük dönüşüm (`ui ↔ i`) **hatadır**; yalnızca
`unsafe` bloğunda geçer ([2.1](02.1-unsafe.md#21-unsafe-bloğu)). Diğer her şey `as` ile açık yazılır.

| Kaynak → Hedef | Örtük? | Not |
|----------------|--------|-----|
| `i8 → i32`, `ui8 → ui32` | evet | genişleme, aynı işaret |
| `i32 → i8` | **hayır** | `as i8` (kırpar) |
| `i32 → ui32`, `ui32 → i32`, `ui8 → i32` | **hata** `E0601` | `as`; `unsafe { }` içinde örtük geçer ([2.1](02.1-unsafe.md#21-unsafe-bloğu)) |
| `i32 → f64` | evet, **uyarı** `W0601` | duyarlılık kaybolabilir; susturma aşağıda |
| `f64 → i32` | **hayır** | `as i32` (sıfıra yuvarlar) |
| `*T → *U` | **hayır** | `as *U` |
| `*T ↔ usize` | **hayır** | `as usize` / `as *T` |

İşaretlilikleri karışık ikili işlem (`i32 + ui32`) ortak tipe örtük dönüşemediği için `E0601`'dir; bir
tarafa `as` yazılır.

```xlang
local big: i64 = 300
local small: i8 = big as i8        // 44 (kırpıldı)
local u: ui32 = (-1) as ui32       // 0xFFFFFFFF
local x: i64 = small               // i8 -> i64 örtük genişleme
local n: ui8 = 5
local m: i32 = n                   // HATA E0601 (işaretlilik değişiyor)
local m2: i32 = n as i32           // OK
```

### `i → f` uyarısı

İşaretli tamsayıdan `f32`/`f64`'e örtük dönüşüm geçer ama `W0601` uyarısı verir: `i32 → f32`, `i64 → f64`
gibi dönüşümlerde büyük değerler tam gösterilemez.

```
warning[W0601]: `i64` değeri `f64`'e örtük dönüştürülüyor, duyarlılık kaybolabilir
  --> app.xl:7
   |
 7 | local f: f64 = total          <======= i64 -> f64: 2^53 üstü değerler yuvarlanır
   |
detay:  `f64` mantisi 53 bit; `total`'ın tipi 64 bit.
neden:  Büyük tamsayılar sessizce yuvarlanabilir.
düzelt: `total as f64` yaz (açık dönüşüm uyarı vermez) ya da bilerek ise @allow(W0601).
```

| Susturma | Kapsam |
|----------|--------|
| `x as f64` (açık dönüşüm) | Uyarı hiç çıkmaz |
| `@allow(W0601)` statement/blok/fonksiyon/dosya üstünde | O kapsam ([19](19-tanilar-ve-uyarilar.md#19-tanılar-ve-uyarılar)) |
| `--allow=W0601` / `--deny=W0601` | Tüm derleme / hataya çevirir |

```xlang
@allow(W0601)
local f: f64 = total               // bu satır için uyarı yok

@allow(W0601)
fn average(sum: i64, n: i64) -> f64 {   // fonksiyon içinde tümü sessiz
    return sum / n
}
```
