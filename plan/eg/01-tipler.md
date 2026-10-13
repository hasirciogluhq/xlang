## 1. Tipler

Tüm tipler açık boyutludur. İşaretli ve işaretsiz tamsayılar ayrı tiplerdir.

| Tip | Boyut | İşaret | C karşılığı |
|-----|-------|--------|-------------|
| `i8`  | 1 B | işaretli | `int8_t` |
| `i16` | 2 B | işaretli | `int16_t` |
| `i32` | 4 B | işaretli | `int32_t` |
| `i64` | 8 B | işaretli | `int64_t` |
| `isize` | pointer genişliği | işaretli | `intptr_t` / `ssize_t` |
| `ui8` | 1 B | işaretsiz | `uint8_t` |
| `ui16` | 2 B | işaretsiz | `uint16_t` |
| `ui32` | 4 B | işaretsiz | `uint32_t` |
| `ui64` | 8 B | işaretsiz | `uint64_t` |
| `usize` | pointer genişliği | işaretsiz | `uintptr_t` / `size_t` |
| `f32` | 4 B | — | `float` |
| `f64` | 8 B | — | `double` |
| `bool` | 1 B | — | `_Bool` / `bool` |
| `void` | — | — | `void` |
| `*T` | pointer genişliği | — | `T*` |
| `fn(A, B) -> R` | pointer genişliği | — | fonksiyon pointer'ı |
| `[T; N]` | `N * size_of(T)` | `T`'ye göre | `T[N]` ([6](06-struct-ve-bellek-duzeni.md#sabit-boyutlu-dizi-t-n)) |
| `enum E: T` | `size_of(T)` | `T`'ye göre | `enum` (alt tip tamsayıysa) ([6.1](06.1-enum.md#61-enum)) |

```xlang
local a: i32 = -5
local b: ui8 = 250
local c: f64 = 3.14
local ok: bool = true
local p: *i32 = &a
```

- Sabit literal'in tipi bağlama göre çıkarılır; belirsizse `i32` (tam) / `f64` (ondalık).
- Taşma **tanımlıdır**: tamsayılar two's complement ile sarar (`ui8: 255 + 1 == 0`).
- Sıfıra bölme çalışma zamanında `panic`'tir (kayıtlı `panic` yoksa trap; bkz. [14.1](14.1-register.md#141-register)).
