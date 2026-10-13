## 2. Tip dönüşümü

Örtük dönüşüm yalnızca **aynı işaretlilikte genişleme**dir. Diğer her şey `as` ile açık yazılır.

| Kaynak → Hedef | Örtük? | Not |
|----------------|--------|-----|
| `i8 → i32`, `ui8 → ui32` | evet | genişleme, aynı işaret |
| `i32 → i8` | **hayır** | `as i8` (kırpar) |
| `i32 → ui32` | **hayır** | `as ui32` (bit deseni korunur) |
| `ui8 → i32` | evet | işaretsiz küçük → işaretli büyük değer korunur |
| `i32 → f64` | evet | |
| `f64 → i32` | **hayır** | `as i32` (sıfıra yuvarlar) |
| `*T → *U` | **hayır** | `as *U` |
| `*T ↔ usize` | **hayır** | `as usize` / `as *T` |

```xlang
local big: i64 = 300
local small: i8 = big as i8        // 44 (kırpıldı)
local u: ui32 = (-1) as ui32       // 0xFFFFFFFF
local x: i64 = small               // i8 -> i64 örtük genişleme
```
