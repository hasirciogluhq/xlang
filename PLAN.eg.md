# xlang — Plan Bitince Dil Nasıl Görünecek

> Bu dosya **dil seviyesi şemadır**: [PLAN.md](PLAN.md) başarıyla tamamlanınca xlang'ın nasıl
> kullanılacağını gösterir. Plan'da dil yüzeyini değiştiren her güncelleme bu dosyaya da yansır.
> Burada compiler iç yapısı yok, sadece kullanıcının yazdığı dil ve CLI var.
>
> Sözdizimi **tasarım önerisidir**; plan ilerledikçe birlikte güncellenir.

İçindekiler: [1 Tipler](#1-tipler) · [2 Tip dönüşümü](#2-tip-dönüşümü) · [3 Fonksiyonlar](#3-fonksiyonlar) ·
[4 Değişkenler ve kontrol akışı](#4-değişkenler-ve-kontrol-akışı) · [5 Pointer](#5-pointer) ·
[6 Struct ve bellek düzeni](#6-struct-ve-bellek-düzeni) · [7 Class](#7-class) · [8 Interface](#8-interface) ·
[9 Koşullu derleme](#9-koşullu-derleme) · [9.1 type](#91-tip-takma-adı-type) ·
[9.2 define/macro](#92-define-ve-macro) · [10 CLI](#10-cli) · [11 Syscall](#11-syscall) ·
[12 Atomic ve fonksiyon pointer'ı](#12-atomic-ve-fonksiyon-pointerı) · [13 Modüller](#13-modüller) ·
[14 Harici (C) semboller](#14-harici-c-semboller) · [15 Stdlib ile thread](#15-stdlib-ile-thread)

---

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

```xlang
local a: i32 = -5
local b: ui8 = 250
local c: f64 = 3.14
local ok: bool = true
local p: *i32 = &a
```

- Sabit literal'in tipi bağlama göre çıkarılır; belirsizse `i32` (tam) / `f64` (ondalık).
- Taşma **tanımlıdır**: tamsayılar two's complement ile sarar (`ui8: 255 + 1 == 0`).
- Sıfıra bölme çalışma zamanında trap'tir.

---

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

---

## 3. Fonksiyonlar

Fonksiyonlar `fn` ile, dönüş tipi **`->`** ile yazılır. `->` yoksa dönüş `void`'dir.

```xlang
fn add(a: i32, b: i32) -> i32 {
    return a + b
}

fn log_value(v: i32) {          // -> yok = void
    // ...
}

export fn public_api(x: ui64) -> ui64 {   // modül dışına açık
    return x * 2
}

fn apply(f: fn(i32) -> i32, v: i32) -> i32 {   // fonksiyon pointer'ı parametresi
    return f(v)
}
```

| Parça | Yazım |
|-------|-------|
| Parametre | `ad: tip` |
| Dönüş tipi | `-> tip` |
| Dışa açma | `export fn` |
| Fonksiyon pointer tipi | `fn(i32, i32) -> i32` |
| Giriş noktası | `fn main() -> i32` |

---

## 4. Değişkenler ve kontrol akışı

```xlang
fn main() -> i32 {
    local i: i32 = 0
    local sum: i64 = 0

    while i < 10 {
        if i % 2 == 0 {
            sum = sum + i
        } else {
            sum = sum - 1
        }
        i = i + 1
    }

    if sum > 0 { return 0 }
    return 1
}
```

| Yapı | Yazım |
|------|-------|
| Yerel değişken | `local ad: tip = değer` (tip çıkarılabilirse atlanır) |
| Koşul | `if c { } else { }` |
| Döngü | `while c { }` |
| Dönüş | `return değer` |

Karşılaştırma işaretlilikten etkilenir: `ui32` için `<` işaretsiz, `i32` için işaretli karşılaştırmadır.

---

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

---

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

Sabit boyutlu dizi (C struct uyumu için):

```xlang
@repr(c)
struct Packet {
    magic: ui32
    data:  [ui8; 16]
}
```

---

## 7. Class

`class`, struct'ın üstüne **metod, kalıtım ve sanal çağrı** ekler. Tek kalıtım vardır.

```xlang
class Shape {
    id: i32

    virtual fn area(self) -> f64 {
        return 0.0
    }
}

class Circle : Shape {
    radius: f64

    override fn area(self) -> f64 {
        return 3.14159 * self.radius * self.radius
    }
}

fn describe(s: *Shape) -> f64 {
    return s.area()          // sanal çağrı: vtable üzerinden
}
```

| Yapı | Anlam |
|------|-------|
| `class B : A` | `A`'dan tek kalıtım |
| `virtual fn` | Sanal metod (vtable'a girer) |
| `override fn` | Taban sanal metodu geçersiz kılar |
| `self` | Metodun nesnesi (`*Sınıf` olarak) |
| `init` / `deinit` | Açık kurucu/yıkıcı metod (ilk sürümde otomatik çağrı yok) |

Bellek: sanal metodu olan class'ın başında **vptr** vardır.

```
Circle nesnesi (x86_64)

  +--------+-------+-----------+
  | vptr   |  id   |  radius   |
  | 8 B    | 4 B   |  8 B      |   (id sonrası 4 B padding: radius 8'e hizalanır)
  +--------+-------+-----------+
      |
      v   Circle vtable
  +-----------------+
  | offset-to-top   |   (Itanium C++ ABI sırası; @repr(c) class'ta)
  | typeinfo        |
  | &Circle.area    |   <- vptr buraya işaret eder
  +-----------------+
```

`@repr(c) class` C++'taki tek kalıtımlı polimorfik sınıfla aynı düzeni verir (Itanium ABI).
Çoklu kalıtım ve sanal kalıtım desteklenmez (açık hata).

---

## 8. Interface

`interface` yalnızca metod imzaları tanımlar. Struct veya class bir interface'i **implement** edebilir.
Interface değeri `{data, itable}` ikilisidir; nesnenin kendi düzenine **dokunulmaz** (C düzeni bozulmaz).

```xlang
interface Writer {
    fn write(self, buf: *ui8, len: usize) -> isize
}

struct FileOut {
    fd: i32
}

impl Writer for FileOut {
    fn write(self, buf: *ui8, len: usize) -> isize {
        return syscall(1, self.fd, buf, len) as isize     // write
    }
}

fn emit(w: Writer, msg: *ui8, n: usize) -> isize {
    return w.write(msg, n)       // itable üzerinden dolaylı çağrı
}

fn main() -> i32 {
    local f: FileOut = FileOut { fd: 1 }
    emit(f, "hi\n", 3)           // FileOut -> Writer: {data=&f, itable=FileOut:Writer}
    return 0
}
```

```
Writer değeri (fat pointer, 16 B)

  +-----------+-----------+
  | data  *   | itable *  |
  +-----------+-----------+
        |           |
        v           v
   FileOut nesnesi  [ &FileOut.write ]      (derleme zamanında üretilen sabit tablo)
```

| Yapı | Anlam |
|------|-------|
| `interface Ad { fn ... }` | Metod imzaları |
| `impl Ad for Tip { ... }` | Tipin interface'i implement etmesi |
| Interface tipli parametre/yerel | `{data, itable}` fat pointer |
| Çağrı | `değer.metod(...)` itable'dan dolaylı |

---

## 9. Koşullu derleme

Metin makrosu **yok** (`#ifdef/#endif` yok). `#if` süslü parantezli, yapısaldır; item, statement ve
struct/class alanı seviyesinde çalışır. Pasif dallar yine parse edilir, ama derlenmez.

```xlang
#if os == "linux" {
    fn page_size() -> usize { return 4096 }
} #elif os == "macos" && arch == "aarch64" {
    fn page_size() -> usize { return 16384 }
} #else {
    #error "desteklenmeyen platform"
}

struct Config {
    verbose: bool
    #if defined(ENABLE_STATS) {
        counter: ui64
    }
}

fn main() -> i32 {
    #if debug {
        log("debug build")
    }
    return 0
}
```

| Kaynak | Örnek | Tür |
|--------|-------|-----|
| Yerleşik | `os`, `arch`, `pointer_width`, `endian`, `debug`, `opt_level` | hedef/derleme ayarından |
| CLI | `-D ENABLE_STATS`, `-D LEVEL=3`, `-D NAME="x"` | bool / tamsayı / string |
| Dosyada | `define LEVEL = 3` ([9.2](#92-define-ve-macro)) | sabit; CLI `-D` bunun yerine geçer |

| İfade | Anlam |
|-------|-------|
| `defined(NAME)` | CLI/yerleşik olarak tanımlı mı |
| `NAME == değer`, `!=`, `<`, `<=`, `>`, `>=` | karşılaştırma |
| `&&`, `\|\|`, `!` | mantıksal |
| `#error "msg"` | dal seçilirse derleme hatası |

Koşullar derleme zamanı sabitidir; çalışma zamanı değişkeni kullanılamaz.

---

## 9.1 Tip takma adı (`type`)

`typedef` karşılığı `type`'tır. Yeni bir tip **yaratmaz**, mevcut tipe ikinci bir ad verir
(şeffaf takma ad): aynı boyut, aynı düzen, aynı tip olarak karşılaştırılır.

```xlang
type Byte = ui8
type Size = usize
type Handler = fn(i32) -> i32          // fonksiyon pointer'ı
type BytePtr = *ui8

export type Fd = i32                   // modül dışına açık

@repr(c)
struct Header { tag: Byte, length: ui32 }

type Hdr = Header                      // struct'a takma ad

fn run(h: Handler, v: i32) -> i32 {
    return h(v)
}
```

| Yazım | Anlam |
|-------|-------|
| `type Ad = Tip` | Takma ad tanımı |
| `export type Ad = Tip` | Dışa açık takma ad |
| Kullanım | Tip yazılan her yerde (`local`, parametre, alan, `as`, `size_of`) |

Takma ad ayrı bir tip değildir: `Byte` ile `ui8` birbirinin yerine geçer, yeni sınıf/ayrı kimlik yoktur.

---

## 9.2 `define` ve macro

Metin değiştiren C önişlemcisi **yok**. `define` yapısaldır: gövde süslü parantez içinde yazılır
(satır sonu için `\` yoktur, gövde zaten çok satırlı olabilir), parametreler **türlüdür**, ve
`#` / `##` yoktur.

### Sabit define

```xlang
define MAX_CLIENTS = 64
define PAGE = 4096
define NAME = "xlang"

local buf: [ui8; PAGE]
```

`define NAME = sabit_ifade`: derleme zamanı sabiti. `#if` koşullarında da kullanılır. CLI'dan
`-D MAX_CLIENTS=128` verilirse dosyadaki tanımın **yerine geçer** (CLI önceliklidir).

### Fonksiyon benzeri macro

```xlang
define square(x: expr) {
    x * x
}

define swap(a: expr, b: expr, T: type) {
    local tmp: T = a
    a = b
    b = tmp
}

fn main() -> i32 {
    local p: i32 = 3
    local q: i32 = 4
    swap!(p, q, i32)
    return square!(p + 1)       // (p + 1) * (p + 1): parametre bir ifade olarak yerleşir, öncelik bozulmaz
}
```

- Çağrı **`ad!(...)`** ile yapılır; fonksiyon çağrısıyla karışmaz.
- Gövde çok satırlı olabilir, devam karakteri yoktur.
- Parametreler parse edilmiş parçalardır; metin yapıştırma olmadığı için `x * x` içinde `p + 1`
  parantez eklemeye gerek kalmadan doğru önceliğe sahiptir.

| Parametre türü | Kabul ettiği |
|----------------|--------------|
| `expr` | Bir ifade |
| `type` | Bir tip |
| `ident` | Bir tanımlayıcı |
| `block` | `{ ... }` bloğu |
| `items` | Bir veya daha fazla üst düzey bildirim |

### Macro ile import / bildirim üretme

Macro gövdesi ifade, statement, bildirim ve hatta `import` içerebilir. Macro bir modül seviyesinde
çağrılırsa bildirimleri üretir:

```xlang
define with_logging(name: ident, body: block) {
    import "std/io" as io
    fn name() {
        io.print("enter {}\n", stringify!(name))
        body
    }
}

with_logging!(startup, {
    init_system()
})
```

| Kural | Açıklama |
|-------|----------|
| Hijyen | Macro içindeki `local` adları çağıranın adlarıyla çakışmaz (otomatik yeniden adlandırılır) |
| Özyineleme | Macro kendini/diğerini çağırabilir; derinlik sınırı 64 |
| Hata konumu | Hata, macro çağrı yerini ve genişleme zincirini gösterir |
| Dışa açma | `export define ...`; `from "m" import square` ile alınır, `square!(...)` olarak çağrılır |
| Koşullu | `#if` içinde tanımlanabilir, macro gövdesinde `#if` kullanılabilir |

### `#` ve `##` yerine adlandırılmış yardımcılar

```xlang
define assert(cond: expr) {
    if !(cond) {
        panic(stringify!(cond))      // kaynak metnini string yapar
    }
}
```

| Yardımcı | Anlam |
|----------|-------|
| `stringify!(parça)` | Parçanın kaynak metnini string literal yapar |
| Tanımlayıcı birleştirme (`##`) | **Yok**; gerekirse macro parametresi olarak `ident` geçirilir |

---

## 10. CLI

```
xlang build main.xlang -o app                       # çalıştırılabilir
xlang build main.xlang --emit=ir                    # LLVM IR
xlang build main.xlang -D ENABLE_STATS -D LEVEL=3   # tanımlar
xlang build main.xlang -U DEBUG_TRACE               # tanımı kaldır
xlang build main.xlang --target aarch64-apple-darwin -O2 -g
xlang build main.xlang --print-cfg                  # çözülmüş tüm tanımları yaz
```

| Bayrak | Anlam |
|--------|-------|
| `-D NAME` / `--define NAME` | `NAME = true` |
| `-D NAME=DEĞER` | Tamsayı/string/bool değer |
| `-U NAME` / `--undef NAME` | Tanımı kaldır |
| `--target <triple>` | Hedef platform; `os`, `arch`, `pointer_width` buradan gelir |
| `-O0` … `-O3` | Optimizasyon seviyesi (`opt_level`) |
| `-g` | Debug info; `debug = true` |
| `--emit=ir\|hir\|obj\|exe` | Hangi aşamada durulacağı |
| `--cpu <ad>` | Hedef CPU (varsayılan `generic`) |
| `--print-cfg` | Etkin tanımları yazdırır |

---

## 11. Syscall

Compiler yalnızca **numarayı ve argümanları CPU trap'ine yerleştirir**; hangi syscall olduğunu bilmez.
Argüman sayısı 0–6 olabilir. Dönüş her zaman `i64`'tür.

```xlang
fn getpid() -> i64 {
    return syscall(39)                    // Linux x86_64: getpid
}

fn write_all(fd: i64, buf: *ui8, n: usize) -> i64 {
    return syscall(1, fd, buf as i64, n as i64)    // write(fd, buf, n)
}
```

| Hedef | Komut | Numara | Argümanlar | Dönüş |
|-------|-------|--------|------------|-------|
| Linux x86_64 | `syscall` | `rax` | `rdi rsi rdx r10 r8 r9` | `rax` |
| Linux aarch64 | `svc #0` | `x8` | `x0 … x5` | `x0` |
| macOS x86_64 | `syscall` | `rax` (+`0x2000000`) | `rdi rsi rdx r10 r8 r9` | `rax` |
| macOS aarch64 | `svc #0x80` | `x16` | `x0 … x5` | `x0` |

Numaralar hedefe özeldir; bunları bir stdlib katmanı `#if os == ...` ile seçer, compiler bilmez.

---

## 12. Atomic ve fonksiyon pointer'ı

Thread/mutex gibi şeyler compiler'da yoktur; yazılabilmeleri için yalnızca atomic işlemler ve
fonksiyon pointer'ı vardır.

```xlang
fn spin_lock(flag: *ui32) {
    while atomic_cas(flag, 0, 1, seq_cst) != 0 { }
}

fn spin_unlock(flag: *ui32) {
    atomic_store(flag, 0, release)
}

fn call_twice(f: fn(i32) -> i32, v: i32) -> i32 {
    return f(f(v))
}

fn inc(x: i32) -> i32 { return x + 1 }

fn main() -> i32 {
    return call_twice(&inc, 0)       // 2
}
```

| Builtin | Anlam |
|---------|-------|
| `atomic_load(p, order)` | Atomik okuma |
| `atomic_store(p, v, order)` | Atomik yazma |
| `atomic_xchg(p, v, order)` | Değiştir, eskiyi döndür |
| `atomic_cas(p, expected, new, order)` | Karşılaştır-değiştir, eski değeri döndür |
| `atomic_add(p, v, order)` | Ekle, eskiyi döndür |
| `fence(order)` | Bellek bariyeri |
| `&fn_name` | Fonksiyon adresi (`fn(...) -> R` tipli) |

Order değerleri: `relaxed`, `acquire`, `release`, `acq_rel`, `seq_cst`.

---

## 13. Modüller

Her `.xlang` dosyası bir modüldür. `export` ile işaretli semboller dışarı açılır.

```xlang
// math.xlang
export fn square(x: i32) -> i32 { return x * x }
fn helper() -> i32 { return 0 }          // private, import edilemez

// main.xlang
import "math" as m                    // tüm modül, m. önekiyle
from "math" import square as sq       // Python tarzı: from <modül> import <sembol>
import cube from "math"              // ters sıra da geçerli: import <sembol> from <modül>

fn main() -> i32 {
    return m.square(3) + sq(2) + cube(1)
}
```

| Yazım | Anlam |
|-------|-------|
| `import "yol" as ad` | Modülü `ad.` önekiyle kullan |
| `from "yol" import a, b as c` | Seçili sembolleri doğrudan al |
| `import a, b as c from "yol"` | Yukarıdakiyle **aynı anlam**, ters sıra |
| `export` | Modül dışına açık |

`from "yol" import ...` ve `import ... from "yol"` birbirinin eşdeğeridir; ikisi de aynı AST düğümüne iner,
biri diğerine tercih edilmez. Alias için her iki biçimde de `as` kullanılır.

---

## 14. Harici (C) semboller

Gövdesiz `declare fn` bir harici (C ABI) sembolü bildirir; link zamanında çözülür. Compiler bu
fonksiyonun ne olduğunu bilmez.

```xlang
declare fn puts(s: *ui8) -> i32
declare fn pthread_create(t: *ui64, attr: *void, entry: fn(*void) -> *void, arg: *void) -> i32

@repr(c)
struct Point { x: f64, y: f64 }

declare fn point_len(p: *Point) -> f64     // C tarafı: double point_len(const struct Point*)
```

C ile veri paylaşılacak struct'lar `@repr(c)` olmalıdır.

---

## 15. Stdlib ile thread

Compiler thread/scheduler bilmez. Stdlib (xlang) platform API'sini çağırır; kullanıcı yalnızca stdlib'i görür.

```xlang
import "std/sched" as sched
import "std/io" as io

struct Job { id: i32 }

fn worker(arg: *void) {
    local j: *Job = arg as *Job
    io.print("job {}\n", j.id)
}

fn main() -> i32 {
    local a: Job = Job { id: 1 }
    local b: Job = Job { id: 2 }
    sched.spawn(worker, &a as *void)
    sched.spawn(worker, &b as *void)
    sched.wait_all()
    return 0
}
```

```
kullanıcı kodu
   │   import "std/sched", "std/thread", "std/io"
   ▼
xlang stdlib            thread · sync · sched · io · mem
   │   #if os == ...
   ▼
std/sys/{linux,macos,windows}     syscall(...)   veya   declare fn pthread_* / CreateThread
   ▼
compiler çekirdeği:  syscall · atomic_* · fn pointer · inline asm
```

`go`/`spawn` anahtar sözcüğü yoktur; hepsi stdlib fonksiyonudur.
