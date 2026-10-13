## 6. Struct ve bellek düzeni

`struct` veri taşır, metodu yoktur. Bellek düzeni iki modda çalışır:

| Mod | Yazım | Davranış |
|-----|-------|----------|
| **xlang düzeni** (varsayılan) | `struct S { ... }` | Compiler alan sırasını/padding'i seçebilir; düzene güvenilmez |
| **C düzeni** | `@repr(c) struct S { ... }` | Alanlar bildirim sırasında, C/C++ hizalama kurallarıyla; `sizeof/offsetof` clang ile aynı |

```xlang
@repr(c)
struct Header {
    tag:    ui8
    length: ui32
    id:     ui16
}
```

C düzeniyle bellekte (x86_64, `@repr(c)`):

```
offset  0   1   2   3   4   5   6   7   8   9   10  11
      +---+---+---+---+---+---+---+---+---+---+---+---+
      |tag|pad|pad|pad|   length (ui32)   | id (ui16) |
      +---+---+---+---+---+---+---+---+---+---+---+---+
      size = 12, align = 4     (C'deki  struct { uint8_t; uint32_t; uint16_t; } ile aynı)
```

Ek öznitelikler:

| Öznitelik | Etki |
|-----------|------|
| `@repr(c)` | C/C++ düzeni |
| `@packed` | Padding yok, hizalama 1 |
| `@align(N)` | Tip hizalamasını en az `N` yapar |

Derleme zamanı doğrulama:

```xlang
static_assert(size_of(Header) == 12, "Header C ile uyumlu olmali")
static_assert(offset_of(Header, length) == 4, "length offset'i yanlis")
static_assert(align_of(Header) == 4, "hizalama")
```

### Sabit boyutlu dizi (`[T; N]`)

Boyutu derleme zamanında sabit olan dizi tipi. C struct'larındaki `T name[N]` alanlarıyla uyum için vardır.

```xlang
define PAGE = 4096

@repr(c)
struct Packet {
    magic: ui32
    data:  [ui8; 16]          // C: uint8_t data[16];
}

local buf: [ui8; PAGE]        // yerel dizi; N bir define sabiti

fn main() -> i32 {
    local a: [i32; 4] = [10, 20, 30, 40]     // dizi literal'i
    local z: [ui8; 16] = [0; 16]             // tekrar biçimi: 16 tane 0
    local i: usize = 2

    a[i] = a[i] + 1                          // indeksleme; i >= 4 ise panic
    local p: *i32 = &a[0]                    // eleman pointer'ı
    return p[1] + a[3]                       // *T de indekslenir (kontrolsüz)
}
```

| Parça | Anlam |
|-------|-------|
| Yazım | `[T; N]` |
| `N` | Derleme zamanı sabit ifadesi (`define` sabiti olabilir) |
| Boyut / hizalama | `size_of([T; N]) == N * size_of(T)`, `align_of([T; N]) == align_of(T)` |
| Uzunluk | `size_of(a) / size_of(T)` (ayrı bir `len` builtin'i yok) |
| Kullanım | Yerel, struct/class alanı, global |
| Literal | `[a, b, c]` ya da `[v; N]`; eleman tipi bağlamdan çıkarılır |
| İndeks tipi | `usize`; `ui8/ui16/ui32` örtük genişler, işaretliler `as usize` ister |
| `a[i]` (dizi) | `--safety=checked` (varsayılan) altında sınır kontrolü: dışındaysa `panic`; `unchecked` ya da `@unchecked fn` kontrolü kaldırır; sabit indeks sınır dışıysa derleme hatası |
| `p[i]` (`*T`) | `*(p + i * size_of(T))`; **hiçbir zaman kontrol edilmez** |
| Değer semantiği | Atama, parametre ve dönüşte dizi tamamen kopyalanır; kopyayı önlemek için `&a` |
| `@repr(c)` struct içinde | C'deki `T[N]` ile aynı düzen |
