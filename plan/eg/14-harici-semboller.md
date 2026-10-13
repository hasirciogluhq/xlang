## 14. Harici (C) semboller

`declare`, **tek bir önektir**: herhangi bir bildirimin başına konur ve "bu başka yerde tanımlı, ben
sadece şeklini söylüyorum" demektir. Fonksiyon, global değişken, struct, class, interface ve
`type` için aynı kural geçerlidir; globaller için ayrı bir sözdizimi yoktur.

| Yazım | Anlam |
|-------|-------|
| `declare fn ad(...) -> T` | Dış fonksiyon (gövdesiz, link'te çözülür) |
| `declare global ad: T` | Dış global değişken |
| `declare struct S { alanlar }` | Düzeni verilmiş dış struct (kod üretmez) |
| `declare struct S` | Opak tip (alanlar bilinmez; yalnızca `*S` ile kullanılır) |
| `declare class C { alanlar; metod imzaları }` | Dış class: metodlar gövdesiz, dış sembol |
| `declare interface I { imzalar }` | Dış interface bildirimi |
| `declare type Ad = Tip` | Dış tarafa ait takma ad |

`declare` içeren her bildirim, normal bildirimlerle **aynı öznitelikleri** alır (`@repr(c)`, `@packed`,
`@align(N)`, `@mangle(...)`). `@repr(c)` **zorunlu değildir**. Dış tarafın xl, C++, Rust, statik kütüphane
ya da `.o` olması fark etmez; compiler kısıtlamaz, yalnızca gerektiğinde uyarı verir ([19](19-tanilar-ve-uyarilar.md#19-tanılar-ve-uyarılar),
[20](20-bellek-duzeni-gecmisi.md#20-bellek-düzeni-geçmişi)).

Düzen belirtilmemiş bir `declare` struct/class/global için sıra:

| Sıra | Durum | Sonuç |
|------|-------|-------|
| 1 | Bağlanan `.o`/`.a`/`.bc` içinde o tipin düzen kaydı var | Kayıttaki düzen kullanılır |
| 2 | Proje düzen geçmişinde (`xl.layout`) var | Kayıttaki düzen kullanılır |
| 3 | Hiçbiri yok | C düzeni varsayılır **ve uyarı** basılır (`W0201`) |

```xlang
declare fn api_log(s: *ui8) -> i32
declare fn api_add(a: i32, b: i32) -> i32       // api.o (C++/Rust/xl) içinden

@repr(c)
declare struct Point { x: f64, y: f64 }          // C: struct Point { double x, y; };

declare fn point_len(p: *Point) -> f64           // C: double point_len(const struct Point*)

@repr(c)
declare global api_argv: **ui8                   // C: extern char** api_argv;

declare struct ApiHandle                         // opak: dış tarafın tipi, alanlar bilinmez
declare fn api_close(h: *ApiHandle) -> i32

// C++ sınıfı: metodlar C++ mangling'iyle bağlanır, adı @mangle ile verilir
@repr(c)
declare class Counter {
    value: i32

    @mangle("_ZN7Counter9incrementEv")
    fn increment(self)
}
```

C/C++ ile veri paylaşılacak struct'ların düzeni için `@repr(c)` en sağlam yoldur, ama zorunlu değildir.

### Global isimler

Kaynak koduna import olmadan gelen isimlerin **iki katmanı** vardır: (a) compiler'ın bildiği kapalı
**builtin** listesi, (b) bir modülde **`register`** ile kaydedilmiş semboller ([14.1](14.1-register.md#141-register)).
Diğer her şey import edilir. **libc yasaktır**: builtin'ler compiler'ın kendisindendir (aşağıya bak).

| Global (import gerekmez) | Neden global |
|--------------------------|--------------|
| `syscall(nr, ...)` | CPU trap'i, başka yolla ifade edilemez; öneksiz, doğrudan `syscall` |
| `__builtin_atomic*`, `__builtin_fence` | Tek CPU talimatları; `__builtin_` önekli (kesin liste Faz 6'da) |
| `__builtin_asm` | Inline asm ([12.1](12.1-inline-asm.md#121-inline-asm)); `__builtin_` önekli |
| `MemoryOrder` | Atomic işlemlerin memory order enum'u ([12](12-atomic-ve-fonksiyon-pointer-i.md#12-atomic-ve-fonksiyon-pointerı)) |
| `sizeOf`, `alignOf`, `offsetOf`, `staticAssert` | Derleme zamanı sorguları |

Builtin fonksiyonlar `syscall`, atomic'ler, `fence` ve `__builtin_asm`'dir. Builtin'ler iki biçimde yazılır ve
**hepsi `__builtin_` önekini taşımaz**: atomic'ler, `fence`, `__builtin_asm` önekli; `syscall`,
`sizeOf` ailesi, `staticAssert` öneksizdir. `syscall` her zaman doğrudan `syscall` olarak yazılır.
Builtin'in gövdesini compiler'ın kendisi sağlar; compiler'ın doğrudan desteklemeyeceği bir şey builtin
olmaz, stdlib'e gider. `memcpy`, `malloc`, `free` builtin değildir, global de değildir: stdlib'den import
edilir; `panic` ise builtin değil **`register`'lıdır** (aşağıda). Bkz. [Karar bekleyen](../03-karar-bekleyen-sonradan-netlesecek.md).

```xlang
from "std/mem" import free                     // stdlib: global değil, import gerekir
from "std/memory" import memcpy

fn cloneHeader(src: *Header) -> *Header {
    local p: *Header = new Header { tag: 0, length: 0, id: 0 }    // kayıtlı allocator
    memcpy(p as *void, src as *void, sizeOf(Header))
    return p
}

fn dropHeader(h: *Header) {
    free(h as *void)
}
```

`memcpy`, `memset`, `memmove`, `memcmp` stdlib'in içinde **xl ile yazılmıştır** ve `export(c)` ile C adıyla
dışa verilir: LLVM'in kendi ürettiği örtük `memcpy` çağrıları da böylece çözülür, libc gerekmez.
`declare` ise yalnızca başka bir nesnede (`api.o`, C++, Rust, statik kütüphane) tanımlı şeyler içindir.

Dil dışarıdan kütüphane almaz: stdlib hiçbir işletim sistemi kütüphanesini (`libc`, `libSystem`, `kernel32`,
pthread) çağırmaz. Her OS için çekirdek ABI'si ham `syscall` ile kullanılır ve OS seçimi `#if os == ...`
ile yapılır ([15](15-stdlib-ile-thread.md#15-stdlib-ile-thread)). `declare` yalnızca kullanıcının kendi bağladığı nesneler içindir.
