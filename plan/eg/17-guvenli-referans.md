## 17. Güvenli referans (`ref T`)

Rust gibi bir borrow checker veya yaşam süresi (lifetime) anotasyonu **yoktur**. Dilin kalıbı bozulmaz:
ham pointer `*T` C gibi aynen çalışır. Üstüne, kullanım-sonrası-serbest (dangling) hatalarını
çalışma zamanında yakalayan **`ref T`** tipi vardır.

| Tip | Boyut | Kontrol | Kullanım |
|-----|-------|---------|----------|
| `*T` | 8 B | yok | FFI, düşük seviye, C uyumlu düzen |
| `ref T` | 16 B (`ptr` + `gen`) | her erişimde nesil kontrolü | güvenli kod |

```xlang
from "std/mem" import free              // stdlib: global değil, import gerekir

struct Node { value: i32 }

fn main() -> i32 {
    local n: ref Node = new Node { value: 7 }     // kayıtlı allocator
    local copy: ref Node = n             // ikisi de aynı nesne

    free(n)                              // nesnenin nesli artar

    return copy.value                    // PANIC: use-after-free (nesil uyuşmuyor)
}
```

Düzen:

```
heap nesnesi                         ref Node (16 B)
+------------+-----------------+     +----------+----------+
| gen (ui64) |  Node { ... }   |     |  ptr  *  |  gen     |
+------------+-----------------+     +----------+----------+
      ^                                    erişimde:  ref.gen == nesne.gen ?  → devam
      └──────── free() gen'i artırır                    değilse panic
```

| Kural | Açıklama |
|-------|----------|
| Oluşturma | `new T { ... }` (kayıtlı allocator), `new(a) T { ... }` (açık allocator) veya `&local` ile |
| Erişim | `r.alan`, `*r`: her erişimde nesil kontrolü |
| `ref T → *T` | Açık `as *T` (korumayı bilerek bırakır) |
| Yerel adresi | `&local` ile alınan `ref`'in fonksiyondan kaçması (return/global'e yazma) **derleme hatası** |
| `null` | `ref T` null olamaz; null gerekiyorsa `*T` kullanılır |
| Maliyet | Erişim başına bir yükleme + karşılaştırma; `--safety=unchecked` ile kontroller kaldırılır |

Per-fonksiyon kapatma: `@unchecked fn hotLoop(...)`.
`ref T`, `@repr(c)` struct alanı ve `export(c)` imzasında **kullanılamaz** (C düzeni bozulur); orada `*T` kullanılır.
