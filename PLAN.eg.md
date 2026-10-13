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
[12 Atomic ve fonksiyon pointer'ı](#12-atomic-ve-fonksiyon-pointerı) · [12.1 Inline asm](#121-inline-asm) ·
[13 Modüller](#13-modüller) ·
[14 Harici (C) semboller](#14-harici-c-semboller) · [15 Stdlib ile thread](#15-stdlib-ile-thread) · [16 Name mangling](#16-name-mangling) ·
[17 Güvenli referans](#17-güvenli-referans-ref-t) · [18 C++ ile kullanım](#18-c-ile-kullanım-ve-çıktı-biçimleri) · [19 Tanılar](#19-tanılar-ve-uyarılar) ·
[20 Düzen geçmişi](#20-bellek-düzeni-geçmişi)

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

### `new`

`new T { alanlar }` **hem bellek ayırır hem nesneyi oluşturur** ve nesnenin pointer'ını (`*T`) döndürür.
Allocator iki yoldan gelir:

| Yazım | Allocator |
|-------|-----------|
| `new T { ... }` | **Varsayılan allocator**: `@default_allocator` ile işaretli global ([aşağıda](#varsayılan-allocator)) |
| `new(a) T { ... }` | Açıkça verilen `a` |

Allocator (`a` ya da varsayılan), `alloc(self, size: usize, align: usize) -> *void` metoduna sahip herhangi
bir değerdir (class, interface değeri ya da `impl` eden struct). Compiler hiçbir allocator'ın gövdesini
bilmez; yalnızca `alloc` çağrısını çözer ve işaretli global'e bakar.

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
}

struct Point { x: i32, y: i32 }

fn main() -> i32 {
    local buf: [ui8; 4096]
    local arena: Bump = Bump { base: &buf as *ui8, used: 0, cap: 4096 }

    local p: *Point = new(&arena) Point { x: 1, y: 2 }     // arena.alloc çağrılır, *Point döner
    return p.x + p.y
}
```

| Yazım | Anlam |
|-------|-------|
| `new(a) T { ... }` | `a.alloc(size_of(T), align_of(T))` çağır, alanları yaz, `*T` döndür |
| `a.alloc` `null` dönerse | `new` de `null` döndürür, alanlar yazılmaz |
| `new T { ... }` | Varsayılan allocator'ın `alloc`'u çağrılır; varsayılan yoksa **hata** (`E0501`) |
| Bırakma | Allocator'ın kendi metodu (ör. `a.free(p)`) ya da `std/mem`'in `free`'si; `new` için ayrı bir kural yok |

### Varsayılan allocator

`@default_allocator` ile işaretlenen **tek bir global**, `new T { ... }`'in kullandığı allocator'dır.
Programda **en fazla bir** işaretli global olabilir. İşaret stdlib'den de kullanıcıdan da gelebilir ama
compiler ikisini ayırt etmez, **öncelik kuralı yoktur**. Stdlib `std/mem`'de bir tane işaretler (stdlib
özelliği `default-allocator`, varsayılan açık). Kendi allocator'ını seçmek isteyen bu özelliği kapatır
(`--std-features=-default-allocator`) ve kendi global'ini işaretler. Compiler adı değil, **işareti** arar.

```xlang
// std/mem.xl (stdlib)
export class Heap {
    fn alloc(self, size: usize, align: usize) -> *void { /* OS'tan sayfa al */ }
}

@default_allocator
export global heap: Heap = Heap { }
```

```xlang
// kullanıcı, stdlib ile: hiçbir şey vermesine gerek yok
struct Point { x: i32, y: i32 }

fn main() -> i32 {
    local p: *Point = new Point { x: 1, y: 2 }     // std/mem'in heap'i
    return p.x + p.y
}
```

```xlang
// --no-std ya da --std-features=-default-allocator: kendi varsayılanını işaretle
global arena_buf: [ui8; 65536]

@default_allocator
global arena: Bump = Bump { base: &arena_buf as *ui8, used: 0, cap: 65536 }

fn main() -> i32 {
    local p: *Point = new Point { x: 1, y: 2 }     // arena.alloc
    return p.x + p.y
}
```

| Durum | Sonuç |
|-------|-------|
| Stdlib var, `default-allocator` açık, kullanıcı işaretlemedi | Stdlib'in `heap`'i |
| Kullanıcı işaretledi, stdlib özelliği kapalı (ya da `--no-std`) | Kullanıcının global'i |
| Toplamda iki işaretli global (stdlib + kullanıcı dahil) | **Hata**; çözüm: stdlib özelliğini kapat |
| Hiç işaretli global yok, `new T { }` yazıldı | **Hata** `E0501` |
| `new(a) T { }` | `a` kullanılır, varsayılan aranmaz |

```
error[E0501]: `new` için varsayılan allocator yok
  --> app.xl:9
   |
 9 | local p: *Point = new Point { x: 1, y: 2 }    <======= varsayılan allocator tanımlı değil
   |
detay:  Stdlib yok (--no-std) ve `@default_allocator` ile işaretli bir global bulunamadı.
neden:  Compiler hiçbir allocator bilmez; `new T` hangi allocator'ı çağıracağını bilemez.
düzelt: `new(a) T { ... }` yaz ya da bir global'i `@default_allocator` ile işaretle.
```

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
```

| Parça | Anlam |
|-------|-------|
| Yazım | `[T; N]` |
| `N` | Derleme zamanı sabit ifadesi (`define` sabiti olabilir) |
| Boyut / hizalama | `size_of([T; N]) == N * size_of(T)`, `align_of([T; N]) == align_of(T)` |
| Kullanım | Yerel, struct/class alanı, global |
| `@repr(c)` struct içinde | C'deki `T[N]` ile aynı düzen |

İndeksleme sözdizimi ve sınır kontrolü henüz belirlenmedi (bkz. `PLAN.md` "Karar bekleyen").

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

Satır tabanlı direktifler: `#if`, `#elif`, `#else`, `#endif`, `#ifdef`, `#ifndef`, `#error`.
Koşul ifadesi, dilin normal `if` ifadesiyle **aynı** sözdizimi ve aynı operatörlerdir; tek fark derleme
zamanında basit bir yorumlayıcıyla değerlendirilmesidir. Direktif, kendi satırının başında `#` ile yazılır.
Pasif bölge yalnızca **token olarak okunur** (lexer'dan geçer), parse edilmez ve derlenmez.

### `#endif` ne zaman gerekli?

- **Gövde aynı satırdaysa `#endif` yazılmaz.** Direktif tek satırı kontrol eder.
- **Gövde alt satıra inerse `#endif` zorunludur.**

```xlang
#if os == "linux" fn page_size() -> usize { return 4096 }     // aynı satır: #endif YOK

#if os == "macos" && arch == "aarch64"                         // gövde alt satırda: #endif GEREKLİ
    fn page_size() -> usize { return 16384 }
    fn cache_line() -> usize { return 128 }
#elif os == "windows"
    fn page_size() -> usize { return 4096 }
#else
    #error "desteklenmeyen platform"
#endif
```

Tek satırlık biçimde `#elif`/`#else` kullanılmaz; birden fazla dal gerekiyorsa blok biçimi (`#endif`'li) yazılır.
Koşul ile gövde aynı satırdaysa ayrım, koşul ifadesinin bittiği yerdir; belirsizse koşul parantezlenir:
`#if (defined(A) && B) return 1`.

```xlang
struct Config {
    verbose: bool
#if defined(ENABLE_STATS)
    counter: ui64
#endif
}

fn main() -> i32 {
#if debug log("debug build")             // tek satır
    return 0
}
```

### Tanım sorguları

| Yazım | Anlam |
|-------|-------|
| `#ifdef NAME` | `#if defined(NAME)` ile aynı |
| `#ifndef NAME` | `#if !defined(NAME)` ile aynı |
| `defined(NAME)` / `!defined(NAME)` | İfade içinde kullanılabilir: `#if defined(A) && !defined(B)` |
| `#elif ifade`, `#else`, `#endif` | Normal `if / else if / else` karşılığı |

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
        io.print("enter\n")
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

### `#` ve `##` yoktur

```xlang
define assert(cond: expr) {
    if !(cond) {
        panic("assertion failed")
    }
}
```

| C önişlemcisi | xl |
|---------------|----|
| `#` (parçayı metne çevirme) | **Yok** |
| `##` (tanımlayıcı birleştirme) | **Yok**; gerekirse macro parametresi olarak `ident` geçirilir |

---

## 10. CLI

```
xlang build main.xl -o app                          # çalıştırılabilir
xlang build main.xl --emit=ir                       # LLVM IR (.ll metin)
xlang build main.xl --emit=bc                       # LLVM bitcode (.bc)
xlang build main.xl --emit=obj                      # nesne dosyası (.o / .obj)
xlang build lib.xl --emit=header                    # C/C++ başlığı (.h)
xlang build main.xl -D ENABLE_STATS -D LEVEL=3      # tanımlar
xlang build main.xl -U DEBUG_TRACE                  # tanımı kaldır
xlang build main.xl --target aarch64-apple-darwin -O2 -g
xlang build main.xl --mangle=none                   # proje seviyesi mangling
xlang build main.xl --safety=checked                # ref T çalışma zamanı kontrolleri
xlang build main.xl --print-cfg                     # çözülmüş tüm tanımları yaz
```

| Bayrak | Anlam |
|--------|-------|
| `-D NAME` / `--define NAME` | `NAME = true` |
| `-D NAME=DEĞER` | Tamsayı/string/bool değer |
| `-U NAME` / `--undef NAME` | Tanımı kaldır |
| `--target <triple>` | Hedef platform; `os`, `arch`, `pointer_width` buradan gelir |
| `-O0` … `-O3` | Optimizasyon seviyesi (`opt_level`) |
| `-g` | Debug info; `debug = true` |
| `--emit=ir\|bc\|obj\|hir\|header\|exe\|static\|shared` | Çıktı biçimi ([18](#18-c-ile-kullanım-ve-çıktı-biçimleri)) |
| `--mangle=xl\|none` | Proje seviyesi mangling varsayılanı ([16](#16-name-mangling)) |
| `--safety=checked\|unchecked` | `ref T` kontrolleri açık/kapalı ([17](#17-güvenli-referans-ref-t)) |
| `--cpu <ad>` | Hedef CPU (varsayılan `generic`) |
| `--no-std` / `--std=<yol>` | Stdlib yok / başka stdlib ([15](#15-stdlib-ile-thread)) |
| `--print-cfg` | Etkin tanımları yazdırır |

---

## 11. Syscall

Compiler yalnızca **numarayı ve argümanları CPU trap'ine yerleştirir**; hangi syscall olduğunu bilmez.
Argüman sayısı **dinamiktir**: compiler kaç argüman verildiyse onu olduğu gibi iletir (3 verildiyse 3).
Sabit bir sınır yoktur; izin verilen en fazla argüman sayısı derlenen hedefin OS/arch'ına göre değişir ve
bu sınır aşılırsa **derleme hatası** verilir. Dönüş her zaman `i64`'tür.

```xlang
fn getpid() -> i64 {
    return syscall(39)                    // 0 argüman: yalnızca numara
}

fn write_all(fd: i64, buf: *ui8, n: usize) -> i64 {
    return syscall(1, fd, buf as i64, n as i64)    // 3 argüman iletilir
}
```

```
error: `syscall` çağrısında 8 argüman var; hedef `x86_64-linux` en fazla 6 argüman alıyor
  --> sys.xl:12:12
   |
12 |     return syscall(9, a, b, c, d, e, f, g, h)    <======= 8 argüman; bu hedef için sınır 6
```

| Hedef | Komut | Numara | Argümanlar | Dönüş |
|-------|-------|--------|------------|-------|
| Linux x86_64 | `syscall` | `rax` | `rdi rsi rdx r10 r8 r9` | `rax` |
| Linux aarch64 | `svc #0` | `x8` | `x0 … x5` | `x0` |
| macOS x86_64 | `syscall` | `rax` (+`0x2000000`) | `rdi rsi rdx r10 r8 r9` | `rax` |
| macOS aarch64 | `svc #0x80` | `x16` | `x0 … x5` | `x0` |
| Windows x86_64 (NT) | `syscall` | `eax` (SSN) | `r10 rdx r8 r9`, fazlası stack | `rax` |

Numaralar hedefe özeldir; bunları bir stdlib katmanı `#if os == ...` ile seçer, compiler bilmez.

Hata bildirimi hedefe göre farklıdır: Linux hatayı `-errno` olarak döndürür, macOS carry bayrağını set edip
pozitif errno döndürür. `syscall`'ın bunu nasıl yansıtacağı henüz belirlenmedi (bkz. `PLAN.md` "Karar bekleyen").

---

## 12. Atomic ve fonksiyon pointer'ı

Thread/mutex gibi şeyler compiler'da yoktur; yazılabilmeleri için yalnızca atomic işlemler,
fonksiyon pointer'ı ve [inline asm](#121-inline-asm) vardır. Atomic fonksiyonlar compiler
builtin'idir ve **`__builtin_` önekiyle** global olarak bulunur. `mutex` stdlib'dedir (`std/sync`),
altında bu atomic'leri kullanır.

```xlang
fn spin_lock(flag: *ui32) {
    while __builtin_atomic_cas(flag, 0, 1, seq_cst) != 0 { }
}

fn spin_unlock(flag: *ui32) {
    __builtin_atomic_store(flag, 0, release)
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
| `__builtin_atomic_load(p, order)` | Atomik okuma |
| `__builtin_atomic_store(p, v, order)` | Atomik yazma |
| `__builtin_atomic_xchg(p, v, order)` | Değiştir, eskiyi döndür |
| `__builtin_atomic_cas(p, expected, new, order)` | Karşılaştır-değiştir, eski değeri döndür |
| `__builtin_atomic_add(p, v, order)` | Ekle, eskiyi döndür |
| `__builtin_fence(order)` | Bellek bariyeri |
| `&fn_name` | Fonksiyon adresi (`fn(...) -> R` tipli) |

Order değerleri (global sabitler, import gerekmez): `relaxed`, `acquire`, `release`, `acq_rel`, `seq_cst`.

---

## 12.1 Inline asm

Compiler'ın ifade edemeyeceği tek CPU talimatları ve stdlib'in thread trampoline'i ile scheduler bağlam
değişimi için **inline asm** zorunludur. Atomic'ler gibi compiler builtin'idir: **`__builtin_` önekli**, global.

```xlang
fn cpu_relax() {
#if arch == "x86_64"
    __builtin_asm("pause")
#elif arch == "aarch64"
    __builtin_asm("yield")
#endif
}

fn read_sp() -> usize {
    local sp: usize = 0
#if arch == "x86_64"
    __builtin_asm("mov {0}, rsp", out(reg) sp)
#elif arch == "aarch64"
    __builtin_asm("mov {0}, sp", out(reg) sp)
#endif
    return sp
}

fn swap_marker(a: *ui64, v: ui64) -> ui64 {
    local old: ui64 = 0
#if arch == "x86_64"
    __builtin_asm("xchg {0}, [{1}]", inout(reg) v, in(reg) a, clobber("memory"))
    old = v
#endif
    return old
}
```

| Parça | Yazım | Anlam |
|-------|-------|-------|
| Çağrı | `__builtin_asm("şablon", işlenenler...)` | Şablon metni olduğu gibi hedef assembler'a gider |
| Yer tutucu | `{0}`, `{1}`, ... | İşlenenler sırasıyla |
| Giriş | `in(reg) ifade` | İfadenin değeri bir register'a konur |
| Çıkış | `out(reg) değişken` | Register'ın sonucu değişkene yazılır |
| Giriş+çıkış | `inout(reg) değişken` | Önce okunur, sonra aynı register'dan yazılır |
| Sabit register | `in("rax") ifade` | Belirli register (hedefe özel ad) |
| Clobber | `clobber("memory")`, `clobber("rcx")` | Talimatın bozduğu şeyler (compiler bunlara güvenmez) |

Kurallar:

| Kural | Açıklama |
|-------|----------|
| Hedefe özel | Şablon o hedefin assembly'sidir; `#if arch == ...` ile seçilir, compiler şablonun içini yorumlamaz |
| Silinmez | Asm bloğu optimizasyonla kaldırılmaz ve yeniden sıralanmaz |
| Hata | Geçersiz register veya şablon hatası hedef assembler'dan gelir ve derleme hatasıdır |
| Kullanım | Çoğunlukla stdlib'de (`std/sys`, `std/sched`); uygulama kodunda gerekmez |

---

## 13. Modüller

Kaynak dosya uzantısı **`.xl`**'dir. Her `.xl` dosyası bir modüldür.

**Varsayılan export yoktur.** `export` yazılmayan hiçbir şey (fonksiyon, tip, global, macro, takma ad)
başka modülden görünmez ve erişilemez; link seviyesinde de içeride (internal) kalır.

```xlang
// math.xl
export fn square(x: i32) -> i32 { return x * x }
fn helper() -> i32 { return 0 }          // export yok: başka modül göremez, çağıramaz

// main.xl
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
| `import * from "yol"` | Modülün tüm export'larını düz biçimde al |
| `import * as ad from "yol"` | `import "yol" as ad` ile aynı: ad alanı altında al |
| `export` | Başka xl modüllerine açık (varsayılan: kapalı) |
| `export(c)` | `export` + C ABI'de **mangle edilmemiş** sembol; C/C++ buradan çağırır ([16](#16-name-mangling), [18](#18-c-ile-kullanım-ve-çıktı-biçimleri)) |

```xlang
// main.xl
import helper from "math"      // HATA: `helper` math.xl'de export edilmemiş
```

`from "yol" import ...` ve `import ... from "yol"` birbirinin eşdeğeridir; ikisi de aynı AST düğümüne iner,
biri diğerine tercih edilmez. Alias için her iki biçimde de `as` kullanılır.

### Çift import ve çakışma

Aynı ada sahip iki bağlama (binding) **hatadır**; hata, çakışmanın **import satırında** verilir
(kullanım yerinde değil).

```xlang
// a.xl: export fn lorem() ...      b.xl: export fn lorem() ...

import * from "a"
import * from "b"                 // HATA: `lorem` hem a hem b'den geliyor

import * as aa from "a"
import * as bb from "b"           // OK: aa.lorem() ve bb.lorem() ayrı ad alanları

import lorem from "a"
import lorem from "b"             // HATA: aynı ad iki kez bağlanıyor
import lorem as lorem2 from "b"   // OK: yeniden adlandırma
```

| Durum | Sonuç |
|-------|-------|
| `import *` + `import *`, ortak ad | Hata |
| `import *` + `import x from` aynı ad | Hata |
| Aynı ad iki kez adlandırılmış import | Hata |
| Yerel tanım ile import edilen aynı ad | Hata |
| `import * as aa` + `import * as bb` | Çalışır (ad alanları ayrı) |
| Aynı varlık iki yoldan gelirse (ör. a ve b, c'deki aynı fonksiyonu yeniden dışa aktarıyor) | Hata değil, tek varlık; ama **detaylı uyarı** (`W0401`) ve log kaydı |

```
warning[W0401]: `lorem` aynı varlık olarak iki yoldan import edildi
  --> app.xl
   |
 1 | import * from "a"      <======= `lorem` burada a üzerinden geliyor (asıl tanım: c.xl:7)
 2 | import * from "b"      <======= ve burada b üzerinden de geliyor (aynı tanım: c.xl:7)
   |
detay:  a.xl ve b.xl, `lorem`'i c.xl'den yeniden dışa aktarıyor; ikisi de aynı sembole çözülüyor.
neden:  Hata değil (tek varlık), ama iki yoldan gelmesi ileride a veya b'nin kendi `lorem`'ini
        tanımlaması durumunda sessiz çakışmaya dönüşebilir.
düzelt: Tek yoldan import et, ya da `import * as aa from "a"` kullan. Bilerek ise: @allow(W0401)
```

Export tarafındaki kural aynıdır: `export * from "a"` ve `export * from "b"` ortak ad veriyorsa hata,
`export * as aa from "a"` ve `export * as bb from "b"` çalışır.

### Dizin modülleri ve yeniden dışa aktarma (`export ... from`)

Bir yol hem dosya hem dizin olabilir: `"std/memory"` önce `std/memory.xl`'e, yoksa
`std/memory/index.xl`'e çözülür (ikisi birden varsa hata). Dizinin giriş dosyası **`index.xl`**'dir.
Giriş dosyası alt modülleri yeniden dışa aktararak "hepsini" verir.

```
std/
  index.xl            # export * from "memory"; export * as cpu from "cpu"; ...
  memory.xl           # (veya memory/index.xl)
  cpu/
    index.xl
  gpu.xl
```

```xlang
// std/index.xl
export * from "memory"                 // memory'nin tüm export'ları düz biçimde std'ye girer
export * as cpu from "cpu"             // std.cpu.<ad>(...) olarak erişilir
export { launch, Device as GpuDevice } from "gpu"   // seçili + yeniden adlandırma
```

| Yazım | Anlam |
|-------|-------|
| `export * from "m"` | `m`'nin tüm export'larını olduğu gibi yeniden dışa aktar (düz) |
| `export * as ns from "m"` | `m`'yi `ns` ad alanı altında yeniden dışa aktar |
| `export { a, b as c } from "m"` | Seçili sembolleri (isteğe bağlı yeni adla) yeniden dışa aktar |
| `export * from "m" except a, b` | Belirtilenler hariç hepsini aktar |
| `#if os == "linux" export * from "sys_linux"` | Koşullu yeniden dışa aktarma (tek satır) |

```xlang
// kullanıcı
import "std" as std                       // hepsi: std.memcpy, std.cpu.<ad>, std.GpuDevice
from "std/memory" import memcpy           // yalnızca bir alt modül
import "std/cpu" as cpu
```

Kurallar:

| Kural | Açıklama |
|-------|----------|
| Yalnızca export edilenler aktarılır | Kaynak modülde `export` olmayan sembol yeniden dışa aktarılamaz (hata) |
| Çakışma | Aynı ada sahip **iki export hata**dır: aynı dosyada iki kez export, farklı dosyalardan aynı ad (`export *` ile gelenler dahil), yerel export ile aktarılanın çakışması. Çözüm: `except`, `as` ile yeniden adlandırma veya `export * as ns` |
| Modül çözümleme | Bir yolun hangi dosyaya çözüleceğine dair birden fazla strateji olacak (ayrıntı henüz planlanmadı) |
| Döngü | Döngüsel yeniden dışa aktarma desteklenir (tek geçişte çözülür), kendi kendini aktarma hata |
| JS'den farklar | `except` filtresi, koşullu (`#if`) export, `declare` öğelerini (fn/global/struct/class) de aktarabilme, `export(c)` yeniden dışa aktarmada korunur |

---

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
ya da `.o` olması fark etmez; compiler kısıtlamaz, yalnızca gerektiğinde uyarı verir ([19](#19-tanılar-ve-uyarılar),
[20](#20-bellek-düzeni-geçmişi)).

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

Kaynak koduna hiçbir import olmadan gelen isimler **kapalı bir builtin listesidir**. Diğer her şey
stdlib'den import edilir; stdlib hiçbir zaman global olamaz. **libc yasaktır**: builtin'ler compiler'ın
kendisindendir (aşağıya bak).

| Global (import gerekmez) | Neden global |
|--------------------------|--------------|
| `syscall(nr, ...)` | CPU trap'i, başka yolla ifade edilemez; öneksiz, doğrudan `syscall` |
| `__builtin_atomic_*`, `__builtin_fence` | Tek CPU talimatları; `__builtin_` önekli (kesin liste Faz 6'da) |
| `__builtin_asm` | Inline asm ([12.1](#121-inline-asm)); `__builtin_` önekli |
| `relaxed`, `acquire`, `release`, `acq_rel`, `seq_cst` | Atomic işlemlerin memory order sabitleri |
| `size_of`, `align_of`, `offset_of`, `static_assert` | Derleme zamanı sorguları |

Builtin fonksiyonlar atomic'ler, `fence` ve `__builtin_asm`'dir. Builtin'ler iki biçimde yazılır ve
**hepsi `__builtin_` önekini taşımaz**: atomic'ler, `fence`, `__builtin_asm` önekli; `syscall`,
`size_of` ailesi, `static_assert` öneksizdir. `syscall` her zaman doğrudan `syscall` olarak yazılır.
Builtin'in gövdesini compiler'ın kendisi sağlar; compiler'ın doğrudan desteklemeyeceği bir şey builtin
olmaz, stdlib'e gider. `memcpy`, `malloc`, `free` builtin değildir, global de değildir: stdlib'den import
edilir. Bkz. `PLAN.md` "Karar bekleyen".

```xlang
from "std/mem" import free                     // stdlib: global değil, import gerekir
from "std/memory" import memcpy

fn clone_header(src: *Header) -> *Header {
    local p: *Header = new Header { tag: 0, length: 0, id: 0 }    // varsayılan allocator
    memcpy(p as *void, src as *void, size_of(Header))
    return p
}

fn drop_header(h: *Header) {
    free(h as *void)
}
```

`memcpy`, `memset`, `memmove`, `memcmp` stdlib'in içinde **xl ile yazılmıştır** ve `export(c)` ile C adıyla
dışa verilir: LLVM'in kendi ürettiği örtük `memcpy` çağrıları da böylece çözülür, libc gerekmez.
`declare` ise yalnızca başka bir nesnede (`api.o`, C++, Rust, statik kütüphane) tanımlı şeyler içindir.

Dil dışarıdan kütüphane almaz: stdlib hiçbir işletim sistemi kütüphanesini (`libc`, `libSystem`, `kernel32`,
pthread) çağırmaz. Her OS için çekirdek ABI'si ham `syscall` ile kullanılır ve OS seçimi `#if os == ...`
ile yapılır ([15](#15-stdlib-ile-thread)). `declare` yalnızca kullanıcının kendi bağladığı nesneler içindir.

---

## 15. Stdlib ile thread

Compiler thread/scheduler bilmez. Varsayılan stdlib (xl ile yazılmış) her OS'un çekirdek ABI'sini ham
`syscall` ile çağırarak thread oluşturur; OS farkı `#if os` ile stdlib içinde çözülür. Stdlib'in içinde
**Go benzeri bir scheduler** vardır (hafif görevler, her CPU için worker, iş çalma).

Önce stdlib'in kendi içinden bir parça (kullanıcı görmez):

```xlang
// std/sys/thread.xl
#if os == "linux"
    // clone(flags, stack, ptid, ctid, tls)
    fn os_thread_create(stack: *void, entry: fn(*void), arg: *void) -> i64 {
        return syscall(CLONE_NR, CLONE_FLAGS, stack as i64, 0, 0, 0)
    }
#elif os == "macos"
    // bsdthread_create(func, arg, stack, pthread, flags)
    fn os_thread_create(stack: *void, entry: fn(*void), arg: *void) -> i64 {
        return syscall(BSDTHREAD_CREATE_NR, entry as i64, arg as i64, stack as i64, 0, 0)
    }
#elif os == "windows"
    // NtCreateThreadEx(...); SSN, -D WIN_BUILD ile seçilen tablodan gelir
    fn os_thread_create(stack: *void, entry: fn(*void), arg: *void) -> i64 {
        return syscall(NT_CREATE_THREAD_EX_SSN, 0, 0, 0, 0)
    }
#endif
```

Kullanıcı kodu:

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
std/sys/{linux,macos,windows}     yalnızca syscall(...), dış kütüphane yok
   ▼
compiler çekirdeği:  syscall · __builtin_atomic_* · fn pointer · inline asm
```

`go`/`spawn` anahtar sözcüğü yoktur; hepsi stdlib fonksiyonudur.

### Stdlib isteğe bağlıdır

Stdlib bir compiler bayrağıyla seçilir; hiçbir şey zorunlu kılınmaz.

| Bayrak | Anlam |
|--------|-------|
| (varsayılan) | Dile gömülü varsayılan stdlib |
| `--no-std` | Stdlib yok: yalnızca dil + global yerleşikler (`syscall`, atomic...) |
| `--std=<yol>` | Başka bir stdlib kök dizini |
| `--std-features=sched,gpu` | Stdlib özelliklerini aç/kapat (ör. `-default-allocator`: stdlib'in `@default_allocator` işaretini kapatır) |

```
xlang build app.xl                       # varsayılan stdlib
xlang build kernel.xl --no-std           # stdlib'siz
xlang build app.xl --std=./mystd         # kendi stdlib'in
```

### Scheduler (Go benzeri)

```
M:N model

   goroutine benzeri görevler (G)         worker thread'ler (M)         OS thread'leri
   +----+ +----+ +----+ +----+            +---------+  +---------+
   | G1 | | G2 | | G3 | | G4 |  ───────►  | worker0 |  | worker1 |  ───►  clone / bsdthread / NtCreateThreadEx
   +----+ +----+ +----+ +----+            +----+----+  +----+----+
        yerel kuyruk  ◄── iş çalma ──►  yerel kuyruk
```

| Parça | Açıklama |
|-------|----------|
| Görev | Kendi stack'i olan hafif birim; bağlam değişimi inline asm ile |
| Worker | CPU sayısı kadar OS thread'i |
| İş çalma | Boş worker, başkasının kuyruğundan görev alır |
| Bloklama | Bloklayıcı I/O, OS olay mekanizması (epoll/kqueue/IOCP benzeri) ile görev park edilir |

---

## 16. Name mangling

Varsayılan olarak mangling **açıktır**: semboller modül ve imza bilgisini taşır, çakışmaz.
Üç seviyede kapatılıp özelleştirilebilir.

```
xl varsayılanı:     math.xl içinde  fn square(x: i32) -> i32     →   _xl$math$square$i32
export(c) ile:                                                     →   square
@mangle("my_sq"):                                                  →   my_sq
```

| Seviye | Yazım | Kapsam |
|--------|-------|--------|
| Proje | `xlang build ... --mangle=none` (`xl` varsayılan) | Tüm derleme |
| Dosya | `@file(mangle = none)` dosyanın en başında | O dosyadaki tüm semboller |
| Fonksiyon | `@mangle(none)` veya `@mangle("ad")` | Tek fonksiyon / global |

Öncelik: **fonksiyon > dosya > proje**.

```xlang
@file(mangle = none)            // bu dosyada hiçbir sembol mangle edilmez

export fn plain() -> i32 { return 0 }       // sembol: plain

@mangle("renamed_sym")
export fn other() -> i32 { return 1 }       // sembol: renamed_sym
```

`export(c)` her zaman C ABI adını (`mangle = none`) kullanır; mangling ayarlarından bağımsızdır.
Aynı sembol adı iki modülde çakışırsa (mangling kapalıyken) link hatası yerine derleme hatası verilir.

---

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
    local n: ref Node = new Node { value: 7 }     // varsayılan allocator
    local copy: ref Node = n             // ikisi de aynı nesne

    free(n)                              // nesnenin nesli artar

    return copy.value                    // ÇALIŞMA ZAMANI TRAP: use-after-free (nesil uyuşmuyor)
}
```

Düzen:

```
heap nesnesi                         ref Node (16 B)
+------------+-----------------+     +----------+----------+
| gen (ui64) |  Node { ... }   |     |  ptr  *  |  gen     |
+------------+-----------------+     +----------+----------+
      ^                                    erişimde:  ref.gen == nesne.gen ?  → devam
      └──────── free() gen'i artırır                    değilse trap
```

| Kural | Açıklama |
|-------|----------|
| Oluşturma | `new T { ... }` (varsayılan allocator), `new(a) T { ... }` (açık allocator) veya `&local` ile |
| Erişim | `r.alan`, `*r`: her erişimde nesil kontrolü |
| `ref T → *T` | Açık `as *T` (korumayı bilerek bırakır) |
| Yerel adresi | `&local` ile alınan `ref`'in fonksiyondan kaçması (return/global'e yazma) **derleme hatası** |
| `null` | `ref T` null olamaz; null gerekiyorsa `*T` kullanılır |
| Maliyet | Erişim başına bir yükleme + karşılaştırma; `--safety=unchecked` ile kontroller kaldırılır |

Per-fonksiyon kapatma: `@unchecked fn hot_loop(...)`.
`ref T`, `@repr(c)` struct alanı ve `export(c)` imzasında **kullanılamaz** (C düzeni bozulur); orada `*T` kullanılır.

---

## 18. C++ ile kullanım ve çıktı biçimleri

Compiler, xl kodunu C++ projesine bağlamayı **kendisi yürütmez**. Compiler yalnızca standart bir
biçim üretir; hangisini, nasıl bağlayacağınıza kullanıcı kendi build script'inde (xmake vb.) karar verir.

### Çıktı biçimleri (`--emit`)

| Değer | Dosya | Ne için |
|-------|-------|---------|
| `obj` | `.o` / `.obj` | Doğrudan linkere |
| `ir` | `.ll` | `clang++` ile derlenebilen LLVM IR (metin) |
| `bc` | `.bc` | LLVM bitcode |
| `header` | `.h` | `export(c)` fonksiyonlarının ve `@repr(c)` tiplerin C/C++ başlığı |
| `exe` | çalıştırılabilir | tek başına program |
| `static` | `.a` / `.lib` | statik kütüphane (nesneleri arşivler) |
| `shared` | `.so` / `.dylib` / `.dll` | dinamik kütüphane |

`static` ve `shared`, `obj` / `ir`'den **ayrı bir konudur**: ilk ikisi çıktının *biçimi*, son ikisi
*kütüphane türü*dür. `static`/`shared` yalnızca kolaylıktır; isterseniz `obj` üretip kendi build
sisteminizle arşivler/bağlarsınız.

### C++ tarafından kullanılacak API

```xlang
// geom.xl
from "std/math" import sqrt

@repr(c)
export struct Vec2 { x: f64, y: f64 }

export(c) fn vec2_len(v: *Vec2) -> f64 {
    return sqrt(v.x * v.x + v.y * v.y)
}
```

`xlang build geom.xl --emit=header` ürettiği başlık:

```c
/* geom.h */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { double x; double y; } Vec2;
double vec2_len(const Vec2* v);
#ifdef __cplusplus
}
#endif
```

```cpp
// main.cpp
#include "geom.h"
int main() { Vec2 v{3.0, 4.0}; return vec2_len(&v) == 5.0 ? 0 : 1; }
```

### Kullanıcının kendi xmake script'i (taslak)

```lua
-- xmake.lua
rule("xl")
    set_extensions(".xl")
    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        local obj = target:objectfile(sourcefile)
        -- "obj" yerine "ir" seçilirse çıktı .ll olur, clang++ ile derlenir
        batchcmds:execv("xlang", {"build", sourcefile, "--emit=obj", "-o", obj})
        table.insert(target:objectfiles(), obj)
        batchcmds:set_depmtime(os.mtime(obj))
    end)

target("app")
    set_kind("binary")                 -- ya da "static" / "shared": kullanıcı seçer
    add_rules("xl")
    add_files("src/*.cpp", "src/*.xl")
```

| Karar | Kimin |
|-------|-------|
| `obj` mı `ir` mi | Kullanıcı (build script) |
| Statik mi dinamik mi | Kullanıcı (build script) |
| Başlık üretimi | İsteğe bağlı, kullanıcı `--emit=header` çağırır |
| Bağlama komutu | Kullanıcının toolchain'i; compiler çağırmaz |

`export(c)` imzalarında yalnızca C uyumlu tipler (`ui8..i64`, `f32/f64`, `bool`, `*T`, `@repr(c)` struct,
fonksiyon pointer'ı) geçebilir; `class`, `interface`, `ref T` bu imzada derleme hatasıdır.

---

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
düzelt: `import * as aa from "a"` / `import * as bb from "b"` ya da `import lorem as lorem_b from "b"`.
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
  3 | import std from "std"
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
  2 | import std from "std"
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
| `@allow(W0201)` | Bildirimin, fonksiyonun veya dosyanın üstünde; o kapsam |
| `--deny=W0201` | Uyarıyı hataya çevirir |
| `--warn-all` / `--allow-all` | Hepsini aç / hepsini sustur |
| `--explain W0201` | Uyarının uzun açıklamasını yazdırır |

Örnek uyarılar:

| Kod | Ne zaman | Varsayılan |
|-----|----------|-----------|
| `W0201` | `declare` tipin düzeni bilinmiyor, C varsayıldı | uyarı |
| `W0202` | `declare` tipin düzeni, kayıtlı düzenle çelişiyor | uyarı |
| `W0203` | Düzen kaydı farklı bir düzen şemasıyla üretilmiş (`v2` ↔ `v3`) | uyarı |
| `W0301` | `declare` edilen sembol bu derlemede çözülemedi (link zamanında aranacak) | uyarı |
| `E0302` | `export(c)` imzası C'de ifade edilemeyen bir tip içeriyor | **hata** |
| `W0401` | Aynı varlık iki farklı yoldan import/export edildi | uyarı |
| `E0401` | Farklı varlıklar aynı ada import/export ediliyor | **hata** |
| `E0501` | `new T { }` için varsayılan allocator yok (`@default_allocator` işaretli global bulunamadı) | **hata** |

---

## 20. Bellek düzeni geçmişi

xl'in varsayılan (xlang) düzeni **sürümlü ve deterministik** bir algoritmadır; compiler geçmişteki
algoritmaların hepsini içinde taşır ve eski bir nesnenin hangi düzenle üretildiğini bilir.
Düzen, derleyici sürümü değiştiğinde sessizce kaymaz.

```
düzen şeması   v1  →  v2  →  v3  (güncel)         Her şema donmuştur; yeni davranış yeni şema olur.
                                                   Eski bir .o "v2 ile üretildi" der; compiler v2'yi bilir.
```

| Konu | Davranış |
|------|----------|
| Şema kimliği | Her xl nesnesi/başlığı/IR'ı kullandığı düzen şemasını taşır (`xl.layout` metaverisi) |
| Düzen kaydı | Dışa aktarılan her tip için alan sırası, offset, boyut, hizalama yazılır |
| Bilerek karıştırma | `@layout(shuffle)` veya sertleştirme bayrağı; kayıt "karıştırıldı + seed" olarak işaretlenir |
| Karıştırılmamışsa | Compiler, kaydı okumadan şema + tip tanımından düzeni **yeniden türetir** |
| Proje geçmişi | `xl.layout` dosyası projedeki tiplerin son bilinen düzenini tutar; derleme bunu kullanır |
| Statik kütüphane / `.o` | Düzen kaydı nesneyle gelir; xl'in kendi stdlib'i kod yerine `.a`/`.o` olarak gelse bile çalışır |
| Başka dillerden nesne | Kayıt yok; `@repr(c)` ya da C varsayımı + uyarı (`W0201`) |

```xlang
// libgeom.a, xl ile derlenmiş ve düzen kaydı taşıyor (şema v3)
declare struct Vec3 { x: f32, y: f32, z: f32 }   // kayıttan okunur, @repr(c) gerekmez

@layout(scheme = v3)                              // şemayı sabitler (yeni şema çıksa bile kayma olmaz)
struct Packet { id: ui16, flag: bool, len: ui64 }
```

| CLI | Anlam |
|-----|-------|
| `--layout-scheme=v3` | Varsayılan düzen şemasını sabitle |
| `--layout-history=<dosya>` | Düzen geçmişi dosyası (varsayılan `xl.layout`) |
| `--layout-record` | Derlemede düzen kaydını (yeniden) yaz |
| `--print-layout <Tip>` | Bir tipin düzenini (offset/boyut/hizalama) yazdır |
