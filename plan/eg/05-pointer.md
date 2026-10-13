## 5. Pointer

```xlang
fn swap(a: *i32, b: *i32) {
    local t: i32 = *a
    *a = *b
    *b = t
}

fn main() -> i32 {
    local x: i32 = 1
    local y: i32 = 2
    swap(&x, &y)               // x == 2, y == 1
    return 0
}
```

| İşlem | Yazım |
|-------|-------|
| Adres alma | `&değer` |
| Dereference | `*p` |
| Pointer tipi | `*T` |
| Alan erişimi | `p.alan` (pointer üzerinden de `.`) |
| Tür değiştirme | `p as *U` |

`null` pointer değeri `0 as *T` ile ya da `null` sabitiyle yazılır.

### `new`

`new T { alanlar }` **hem bellek ayırır hem nesneyi oluşturur** ve nesnenin pointer'ını (`*T`) döndürür.
Allocator iki yoldan gelir:

| Yazım | Allocator |
|-------|-----------|
| `new T { ... }` | **Kayıtlı allocator**: `register(allocator)` ile kaydedilmiş global ([aşağıda](05-pointer.md#kayıtlı-allocator)) |
| `new(a) T { ... }` | Açıkça verilen `a` |

Allocator, `std/mem`'in **`Allocator` interface'i**dir: `alloc(self, size: usize, align: usize) -> *void`
ve `free(self, p: *void)`. Bir tipin allocator olarak kullanılabilmesi için **`impl Allocator for Tip`
yazılmış olmalıdır**; metodlar tutsa bile `impl` yoksa hatadır (`E0504`), yapısal eşleşme yoktur
([8](08-interface.md#8-interface)). Bırakma her zaman aynı allocator'ın `free`'si ile yapılır; böylece
bellek ayıran ile bırakan ayrışmaz. Compiler hiçbir allocator'ın gövdesini bilmez; yalnızca `alloc` çağrısını
çözer ve kayıtlı global'e bakar.

`new(a) T { x: 1 }` şuna eşittir:

```xlang
local p: *T = a.alloc(sizeOf(T), alignOf(T)) as *T
*p = T { x: 1 }
```

```xlang
class Bump {
    base: *ui8
    used: usize
    cap:  usize
}

impl Allocator for Bump {                                    // zorunlu: yoksa new(&arena) hata (E0504)
    fn alloc(self, size: usize, align: usize) -> *void {     // hizalama bu örnekte yok sayıldı
        if self.used + size > self.cap { return null }
        local p: *void = (self.base as usize + self.used) as *void
        self.used = self.used + size
        return p
    }

    fn free(self, p: *void) { }                              // arena: tek tek bırakılmaz
}

struct Point { x: i32, y: i32 }

fn main() -> i32 {
    local buf: [ui8; 4096]
    local arena: Bump = Bump { base: &buf[0], used: 0, cap: 4096 }

    local p: *Point = new(&arena) Point { x: 1, y: 2 }     // arena.alloc çağrılır, *Point döner
    local sum: i32 = p.x + p.y
    arena.free(p as *void)                                 // aynı allocator
    return sum
}
```

| Yazım | Anlam |
|-------|-------|
| `new(a) T { ... }` | `a.alloc(sizeOf(T), alignOf(T))` çağır, alanları yaz, `*T` döndür |
| `a.alloc` `null` dönerse | `new` de `null` döndürür, alanlar yazılmaz |
| `new T { ... }` | Kayıtlı allocator'ın `alloc`'u çağrılır; kayıtlı yoksa **hata** (`E0501`) |
| Bırakma | `a.free(p)` (açık allocator); kayıtlı allocator için `std/mem`'in `free(p)`'si (aynı allocator'a gider) |
| `a`'nın tipi `Allocator` değil ve `impl Allocator for T` yok | **Hata** `E0504` |

### Kayıtlı allocator

`register(allocator)` ile kaydedilen **tek bir global**, `new T { ... }`'in kullandığı allocator'dır
([14.1](14.1-register.md#141-register)). Programda **en fazla bir** tane kayıtlı olabilir; kayıt stdlib'den de kullanıcıdan
da gelebilir, compiler ikisini ayırt etmez, **öncelik kuralı yoktur**. Global **değiştirilebilir**
(mutable) olduğu için çalışma zamanında `main`'de **bir kez** başka allocator'a geçirilebilir; `new` ve
`free` o andan sonra yeni allocator'ı kullanır, böylece ayıran ve bırakan hep aynı allocator'dır.

Allocator'ı seçmenin iki yolu vardır:

| Yol | Ne zaman | Nasıl |
|-----|----------|-------|
| Çalışma zamanında bir kez | Stdlib var, başka allocator isteniyor | `main`'in başında `setAllocator(a)` |
| Derleme zamanında kayıt | `--no-std` ya da stdlib'in allocator'ı istenmiyor | `--std-features=-default-allocator` + kendi `register(allocator)` global'in |

```xlang
// std/mem.xl (stdlib)
export interface Allocator {
    fn alloc(self, size: usize, align: usize) -> *void
    fn free(self, p: *void)
}

class Heap { }

impl Allocator for Heap {
    fn alloc(self, size: usize, align: usize) -> *void { /* OS'tan sayfa al */ }
    fn free(self, p: *void) { /* OS'a geri ver */ }
}

global heapObj: Heap = Heap { }
global allocatorSet: bool = false

register(allocator) global currentAllocator: Allocator = heapObj     // kayıtlı, değiştirilebilir

export fn free(p: *void) {
    currentAllocator.free(p)                  // her zaman şimdiki allocator'a
}

export fn setAllocator(a: Allocator) {
    if allocatorSet { panic("allocator zaten ayarlandi") }
    allocatorSet = true
    currentAllocator = a
}
```

```xlang
// kullanıcı, stdlib ile: hiçbir şey yazmaya gerek yok
struct Point { x: i32, y: i32 }

fn main() -> i32 {
    local p: *Point = new Point { x: 1, y: 2 }     // currentAllocator.alloc → Heap
    return p.x + p.y
}
```

```xlang
// kullanıcı, çalışma zamanında bir kez kendi allocator'ını verir
from "std/mem" import setAllocator, free

fn main() -> i32 {
    local buf: [ui8; 65536]
    local arena: Bump = Bump { base: &buf[0], used: 0, cap: 65536 }
    setAllocator(arena)                           // main'in başında, ilk new'den önce

    local p: *Point = new Point { x: 1, y: 2 }     // arena.alloc
    free(p as *void)                               // arena.free: aynı allocator
    return 0
}
```

```xlang
// --no-std ya da --std-features=-default-allocator: derleme zamanında kendi kaydın
global arenaBuf: [ui8; 65536]

register(allocator) global arena: Bump = Bump { base: &arenaBuf[0], used: 0, cap: 65536 }

fn main() -> i32 {
    local p: *Point = new Point { x: 1, y: 2 }     // arena.alloc
    return p.x + p.y
}
```

| Durum | Sonuç |
|-------|-------|
| Stdlib var, `default-allocator` açık | Kayıtlı allocator stdlib'in `currentAllocator`'ı (`Heap`) |
| `main`'de `setAllocator(a)` | Bundan sonra `new T` ve `free` `a`'yı kullanır; ikinci çağrı `panic` |
| Kullanıcı `register(allocator)` yaptı, stdlib özelliği kapalı (ya da `--no-std`) | Kullanıcının global'i |
| Toplamda iki `register(allocator)` (stdlib + kullanıcı dahil) | **Hata** `E0502`; çözüm: stdlib özelliğini kapat |
| Kayıtlı yok, `new T { }` yazıldı | **Hata** `E0501` |
| `new(a) T { }` | `a` kullanılır, kayıtlı aranmaz |

`setAllocator` ilk `new`'den **önce**, `main`'in başında çağrılmalıdır. Daha önce ayrılmış bloklar eski
allocator'dan gelmiştir; sonradan `free` edilirse yeni allocator'a gider ve bu tespit edilmez.

```
error[E0501]: `new` için kayıtlı allocator yok
  --> app.xl:9
   |
 9 | local p: *Point = new Point { x: 1, y: 2 }    <======= kayıtlı allocator yok
   |
detay:  Stdlib yok (--no-std) ve `register(allocator)` ile kaydedilmiş bir global bulunamadı.
neden:  Compiler hiçbir allocator bilmez; `new T` hangi allocator'ı çağıracağını bilemez.
düzelt: `new(a) T { ... }` yaz ya da bir global'i `register(allocator)` ile kaydet.
```
