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

Allocator bir **gruptur**: `alloc(self, size: usize, align: usize) -> *void` ve `free(self, p: *void)`
metodlarının ikisine de sahip herhangi bir değer (class, interface değeri ya da `impl` eden struct).
Bırakma her zaman aynı gruptaki `free` ile yapılır; böylece bellek ayıran ile bırakan ayrışmaz. Compiler
hiçbir allocator'ın gövdesini bilmez; yalnızca `alloc` çağrısını çözer ve kayıtlı global'e bakar.

`new(a) T { x: 1 }` şuna eşittir:

```xlang
local p: *T = a.alloc(size_of(T), align_of(T)) as *T
*p = T { x: 1 }
```

```xlang
class Bump {
    base: *ui8
    used: usize
    cap:  usize

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
    arena.free(p as *void)                                 // aynı grup
    return sum
}
```

| Yazım | Anlam |
|-------|-------|
| `new(a) T { ... }` | `a.alloc(size_of(T), align_of(T))` çağır, alanları yaz, `*T` döndür |
| `a.alloc` `null` dönerse | `new` de `null` döndürür, alanlar yazılmaz |
| `new T { ... }` | Kayıtlı allocator'ın `alloc`'u çağrılır; kayıtlı yoksa **hata** (`E0501`) |
| Bırakma | `a.free(p)` (açık allocator); kayıtlı allocator için `std/mem`'in `free(p)`'si (aynı gruba gider) |

### Kayıtlı allocator

`register(allocator)` ile kaydedilen **tek bir global**, `new T { ... }`'in kullandığı allocator'dır
([14.1](14.1-register.md#141-register)). Programda **en fazla bir** tane kayıtlı olabilir; kayıt stdlib'den de kullanıcıdan
da gelebilir, compiler ikisini ayırt etmez, **öncelik kuralı yoktur**. Global **değiştirilebilir**
(mutable) olduğu için çalışma zamanında `main`'de **bir kez** başka allocator'a geçirilebilir; `new` ve
`free` o andan sonra yeni allocator'ı kullanır, böylece ayıran ve bırakan hep aynı gruptur.

Allocator'ı seçmenin iki yolu vardır:

| Yol | Ne zaman | Nasıl |
|-----|----------|-------|
| Çalışma zamanında bir kez | Stdlib var, başka allocator isteniyor | `main`'in başında `set_allocator(a)` |
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

global heap_obj: Heap = Heap { }
global allocator_set: bool = false

register(allocator) global current_allocator: Allocator = heap_obj     // kayıtlı, değiştirilebilir

export fn free(p: *void) {
    current_allocator.free(p)                  // her zaman şimdiki allocator'a
}

export fn set_allocator(a: Allocator) {
    if allocator_set { panic("allocator zaten ayarlandi") }
    allocator_set = true
    current_allocator = a
}
```

```xlang
// kullanıcı, stdlib ile: hiçbir şey yazmaya gerek yok
struct Point { x: i32, y: i32 }

fn main() -> i32 {
    local p: *Point = new Point { x: 1, y: 2 }     // current_allocator.alloc → Heap
    return p.x + p.y
}
```

```xlang
// kullanıcı, çalışma zamanında bir kez kendi allocator'ını verir
from "std/mem" import set_allocator, free

fn main() -> i32 {
    local buf: [ui8; 65536]
    local arena: Bump = Bump { base: &buf[0], used: 0, cap: 65536 }
    set_allocator(arena)                           // main'in başında, ilk new'den önce

    local p: *Point = new Point { x: 1, y: 2 }     // arena.alloc
    free(p as *void)                               // arena.free: aynı grup
    return 0
}
```

```xlang
// --no-std ya da --std-features=-default-allocator: derleme zamanında kendi kaydın
global arena_buf: [ui8; 65536]

register(allocator) global arena: Bump = Bump { base: &arena_buf[0], used: 0, cap: 65536 }

fn main() -> i32 {
    local p: *Point = new Point { x: 1, y: 2 }     // arena.alloc
    return p.x + p.y
}
```

| Durum | Sonuç |
|-------|-------|
| Stdlib var, `default-allocator` açık | Kayıtlı allocator stdlib'in `current_allocator`'ı (`Heap`) |
| `main`'de `set_allocator(a)` | Bundan sonra `new T` ve `free` `a`'yı kullanır; ikinci çağrı `panic` |
| Kullanıcı `register(allocator)` yaptı, stdlib özelliği kapalı (ya da `--no-std`) | Kullanıcının global'i |
| Toplamda iki `register(allocator)` (stdlib + kullanıcı dahil) | **Hata** `E0502`; çözüm: stdlib özelliğini kapat |
| Kayıtlı yok, `new T { }` yazıldı | **Hata** `E0501` |
| `new(a) T { }` | `a` kullanılır, kayıtlı aranmaz |

`set_allocator` ilk `new`'den **önce**, `main`'in başında çağrılmalıdır. Daha önce ayrılmış bloklar eski
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
