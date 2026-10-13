# xlang Compiler Planı

> Dil yüzeyi (kullanıcıya görünen sözdizimi/özellikler) için bkz. **[PLAN.eg.md](PLAN.eg.md)**.
> Bu planda dil seviyesi bir şey değişirse `PLAN.eg.md` de aynı değişiklikte güncellenir.

---

## 0. Temel kurallar (her şeyden önce)

1. **Geriye ve ileriye uyumluluk YOK.** Compiler hiç kullanılmadı. Sözdizimi, AST, CLI, IR,
   dosya yapısı, her şey kökten değiştirilebilir. "Eski davranışı koru" diye bir gereksinim yok.
2. **`examples/`, `test/xlang/`, stdlib/runtime `.xlang` dosyaları tek kullanımlık.** Kırılabilir,
   silinebilir. Compiler hedeflendiği gibi çalıştıktan sonra yeni dil için yeniden yazılır.
   Bu dosyalar **okunmaz**, tasarıma girdi olmaz, bir şeyi onlara uydurmak için çalışılmaz.
3. **`.cursor/` altındaki kurallar/planlar okunmaz.** Tek girdi kullanıcının yönergeleridir.
4. **Compiler yalnızca dili ve CPU primitiflerini bilir.** OS, thread, scheduler, print, malloc
   kavramı compiler'da yoktur; bunlar xlang stdlib'dir.
5. **Önce compiler doğru çalışsın.** Stdlib/example işleri compiler hedefine ulaştıktan sonra gelir.
6. **Plan ve dil şeması senkron kalır:** dil seviyesini etkileyen her plan değişikliği
   `PLAN.eg.md`'yi de günceller (bkz. `CLAUDE.md`).

---

## 1. Hedef hat

```
Source ─► Lexer ─► Parser ─► AST ─► Cfg eliminate (#if) ─► Module resolve
       ─► Sema (tip, isim, layout) ─► HIR ─► LLVM IR ─► Optimize ─► Object ─► Link
```

## 2. Tasarım kararları (özet)

| Konu | Karar | Neden |
|------|-------|-------|
| Syscall | Builtin `syscall(nr, args...)` (yapıldı) | Compiler hangi syscall olduğunu bilmez |
| Fonksiyon sözdizimi | `fn name(a: i32) -> i32 { }`; `->` yoksa dönüş `void` | İstek: `=>` değil `->` |
| Tamsayı tipleri | `i8 i16 i32 i64 isize` / `ui8 ui16 ui32 ui64 usize` | İşaretli/işaretsiz ayrımı; kullanıcı yazımı `ui32` |
| Kayan nokta | `f32`, `f64` | |
| Diğer tipler | `bool`, `void`, `*T` (pointer), `fn(A) -> R` (fonksiyon pointer'ı), `struct`, `class`, `interface` | |
| Örtük dönüşüm | Sadece aynı işaretlilikte genişleme; diğer her şey açık `as` | Sessiz işaret/daralma hataları olmasın |
| Taşma | İşaretli ve işaretsiz tamsayılar sarar (two's complement), bölme sıfıra = trap | Tanımlı davranış |
| Bellek düzeni | Varsayılan: **xlang düzeni** (derleyici alan sırasını/padding'i seçebilir). `@repr(c)` ile işaretli struct/class **C/C++ düzenine** birebir uyar | İstek |
| Class | Tek kalıtım, sanal metod, vptr; `@repr(c)` ile Itanium C++ ABI düzeni (tek kalıtım) | İstek: compiler class bilsin |
| Interface | Fat pointer `{data, itable}` (Go/Rust tarzı); struct ve class implement edebilir | Düzeni (C uyumu) bozmaz |
| Import | `import "m" as a`, `from "m" import x` **ve** `import x from "m"` (ikisi de geçerli, aynı AST) | İstek: iki sıra da desteklensin |
| Koşullu derleme | `#if` / `#elif` / `#else` **süslü parantezli, yapısal** blok; metin makrosu yok | `#ifdef/#endif` karmaşasından kaçınmak |
| Tip takma adı | `type Ad = Tip` (şeffaf alias, yeni tip yaratmaz), `export type` | İstek: typedef benzeri |
| define / macro | `define N = sabit` ve `define f(x: expr) { ... }`, çağrı `f!(...)`; gövde süslü parantezli, `\` yok, `#`/`##` yok, türlü parametre (`expr/type/ident/block/items`), hijyenik; `stringify!`; macro bildirim ve `import` üretebilir, `export define` ile import edilebilir | İstek: C önişlemcisinin pisliği olmadan macro |
| CLI tanımları | `-D/--define NAME[=DEĞER]`, `-U/--undef NAME`, + yerleşik `os`, `arch`, ... | İstek |
| Thread/scheduler/IO/alloc | xlang stdlib; compiler görmez (Faz 7) | Compiler yalnızca düşük seviye bilir |
| Pass manager | New PassManager (`PassBuilder`) | Legacy PM deprecated |
| Derleme modeli | Whole-program, tek LLVM modülü; ayrı derleme en sona | Basit, doğru |
| HIR | Eklenecek | Sema çıktısı; codegen yalnızca tipli ağacı görür |
| LIR | Eklenmeyecek | LLVM IR zaten LIR; ikinci düşük seviye IR kazanç sağlamaz. Koşul: ikinci backend ya da dile özgü CFG analizi → o zaman MIR |

### Karar bekleyen / sonradan netleşecek

- `ui32` yazımı kullanıcı isteğiyle seçildi; `u32`'ye çevirmek yalnızca tip tablosunu değiştirir.
- Sabit boyutlu diziler (`[T; N]`) C uyumlu struct'lar için gerekli; ayrı bir madde olarak eklenecek (Faz 2).
- Generic'ler ve closure kapsam dışı (ilk sürümde yok).

---

## Faz 0 — Temizlik ve güvenlik ağı

- [ ] **0.1** `llvm::verifyModule()` — `generate.cpp`'de `release()` öncesi; hata → IR dump + hata.
- [ ] **0.2** Eski test/example'a bağımlı hiçbir build adımı kalmasın; derleyici kendi küçük
  IR/parse testleriyle doğrulanır (`test/compiler/`, yeni).
- [ ] **0.3** Ölü kod: eski dil yüzeyinin compiler'daki izleri (`declare syscall`, `@syscall`,
  `=>` ya da `:` dönüş tipi, `BigInt`/`Char`/`String` gibi kaldırılacak tipler) planlı olarak
  Faz 1–2'de silinir; eski sözdizimi için uyumluluk katmanı yazılmaz.

## Faz 1 — Frontend yeniden yazımı (yeni sözdizimi)

- [ ] **1.1** Lexer: `->`, `#if/#elif/#else`, `@attr`, yeni tip anahtar sözcükleri (`i8..i64`, `ui8..ui64`,
  `isize`, `usize`, `f32`, `f64`), `class`, `interface`, `impl`/`:` kalıtım sözdizimi.
- [ ] **1.2** Parser: `fn ... -> T`, öznitelikler (`@repr(c)`, `@align(N)`, `@packed`), `class`/`interface`
  bildirimleri, `declare fn ... -> T`, fonksiyon pointer tipi `fn(A, B) -> R`.
- [ ] **1.2b** Import biçimleri: `import "m" as a`, `from "m" import x [as y], ...` ve
  `import x [as y], ... from "m"`; ikincisi birincisiyle aynı `ImportDecl` düğümüne iner.
  Parser, `import` sonrasında string mi tanımlayıcı mı geldiğine bakarak ayırır.
- [ ] **1.2c** `type Ad = Tip` (+ `export type`), `define N = ifade`, `define f(p: tür, ...) { gövde }`,
  macro çağrısı `ad!(...)`. Macro gövdesi ham token ağacı olarak saklanır (parse genişleme anında,
  parametre türüne göre).
- [ ] **1.3** AST'yi yeniden tasarla: tip düğümü `Type` ağaç yapısı (`TypeKind::Int{bits, signed}`,
  `Float{bits}`, `Pointer`, `FnPtr`, `Struct`, `Class`, `Interface`). Eski `Int32/Int64/BigInt` ayrımı gider.
- [ ] **1.4** Diagnostics altyapısı (`include/xlang/util/diagnostics.h`): `Diagnostic{severity, span,
  message, notes}`, snippet'li çıktı, birden fazla hata toplama.

**Kabul:** yeni sözdizimli küçük programlar parse edilir ve `xlang parse` ile AST özeti basılır.

## Faz 2 — Tip sistemi ve bellek düzeni (layout)

- [ ] **2.1** Tip modeli: boyut/hizalama (`size_of`, `align_of`) hedefe (`DataLayout`/pointer genişliği) göre hesaplanır.
- [ ] **2.2** İşaretli/işaretsiz semantik: `sdiv/udiv`, `srem/urem`, `sext/zext`, `icmp slt/ult`,
  `ashr/lshr` — tip bazlı seçim. Örtük dönüşüm kuralları sema'da (tek yerde).
- [ ] **2.3** Layout motoru (yeni `src/sema/layout.cpp`):
  - `@repr(c)`: alanlar bildirim sırasında, C kurallarıyla hizalama/padding, `sizeof` = C `sizeof`.
  - varsayılan: derleyici serbestçe sıralayabilir (ör. büyükten küçüğe) — spesifikasyon verilmez.
  - `@packed`, `@align(N)` desteği.
  - Üretilen LLVM struct tipleri, hesaplanan offset'lerle birebir uyuşur (gerekirse açık padding alanı).
- [ ] **2.4** Derleme zamanı sorguları: `size_of(T)`, `align_of(T)`, `offset_of(T, alan)`; `static_assert(cond, "msg")`.
  C başlığıyla layout doğrulaması bunlarla yapılır.
- [ ] **2.5** Sabit boyutlu dizi tipi `[T; N]` (C struct uyumu için).

**Kabul:** `@repr(c)` struct'lar için clang'ın ürettiği `sizeof/alignof/offsetof` ile test
(birkaç karma tipli struct, iç içe, packed, aligned) birebir uyuşur; x86_64 ve aarch64 için.

## Faz 3 — Koşullu derleme, define/macro ve CLI tanımları

- [ ] **3.1** `#if cond { ... } #elif cond { ... } #else { ... }`: item, statement ve struct/class alan
  seviyesinde kullanılabilir. Metin değil **AST** düzeyinde çalışır; pasif dallar yine parse edilir
  (sözdizimi doğrulanır) ama sema/codegen'e girmez.
- [ ] **3.2** Koşul ifadesi: derleme zamanı sabit ifade; `&& || !`, `== !=`, `< <= > >=`,
  tamsayı/string/bool sabitleri, tanımlı mı sorgusu `defined(NAME)`.
- [ ] **3.3** Yerleşik tanımlar (hedeften türer): `os` (`"linux"|"macos"|"windows"`), `arch`
  (`"x86_64"|"aarch64"`), `pointer_width` (`32|64`), `endian` (`"little"|"big"`), `debug` (bool),
  `opt_level` (0..3).
- [ ] **3.4** CLI bayrakları (`src/cli`):

  | Bayrak | Anlam |
  |--------|-------|
  | `-D NAME`, `--define NAME` | `NAME = true` |
  | `-D NAME=DEĞER`, `--define NAME=DEĞER` | `NAME` tamsayı/string/bool değeri |
  | `-U NAME`, `--undef NAME` | Tanımı (yerleşik dahil olmayan) kaldırır |
  | `--target <triple>` | Hedef triple; `os/arch/pointer_width` buradan türer |
  | `-O0..-O3` | Optimizasyon seviyesi (`opt_level`) |
  | `-g` | Debug info (`debug = true`) |
  | `--emit=ir\|hir\|obj\|exe` | Çıktı aşaması |
  | `--print-cfg` | Çözülmüş tüm tanımları yazdır |
  | `--cpu <name>` | Hedef CPU (varsayılan `generic`) |

- [ ] **3.5** `#error "mesaj"`: koşul dalı seçilirse derleme hatası.
- [ ] **3.6** `define` sabitleri: dosya içi `define N = ifade` ve CLI `-D` aynı isim uzayında; CLI öncelikli;
  `#if` ve normal kodda okunabilir. Sabit ifade değerlendirici (tamsayı/bool/string) burada yazılır.
- [ ] **3.7** Macro genişletme (`src/lang/macro/`): token ağacı üzerinde, metin birleştirme yok.
  - Parametre türü doğrulama (`expr`, `type`, `ident`, `block`, `items`).
  - Hijyen: macro içi `local` adları taze isimle yeniden adlandırılır.
  - Genişleme bir fixpoint döngüsüdür (macro → `#if` eleme → tekrar macro), derinlik sınırı 64,
    hatalar genişleme zincirini ve çağrı konumunu gösterir.
  - Macro gövdesi item/statement/`import` üretebilir; modül seviyesinde çağrılan macro bildirim üretir.
  - `export define` ve `from "m" import ad` ile modüller arası macro paylaşımı; modül yükleme sırasında çözülür.
  - `stringify!(parça)` yerleşik; `#`/`##` ve satır devam karakteri sözdiziminde **yoktur**.
- [ ] **3.8** `type` takma adları sema'da çözülür (Faz 4.1 scope'unda), HIR'da hedef tipe indirgenir;
  döngüsel takma ad hatası.

**Kabul:** `-D DISABLE_X` ile bir item/alan/statement elenir; `--print-cfg` doğru değerleri gösterir.

## Faz 4 — Sema ve HIR

- [ ] **4.1** Sembol tablosu/scope: modül, fonksiyon, blok; nitelikli isim (`alias.name`).
- [ ] **4.2** Sema: isim çözümleme, tip kontrolü, overload yok/var kararı (önerim: overload yok, ad bazlı
  tek tanım, mangling basit), `syscall(...)` argüman doğrulaması (1–7, tamsayı/pointer), `static_assert`.
- [ ] **4.3** HIR (`include/xlang/hir/hir.h`): tipler çözülmüş, örtük dönüşümler açık `Cast`,
  sugar düşürülmüş, sınıf metodları `fn(self: *T, ...)`, interface çağrıları açık itable çağrısı.
- [ ] **4.4** Class:
  - Tek kalıtım, `class B : A`; `virtual` metod, `override`; vptr nesnenin başında.
  - `@repr(c)` class: Itanium düzeni (vptr offset 0; vtable'da offset-to-top ve typeinfo slotları,
    ardından fonksiyonlar). Çoklu/sanal kalıtım ve MSVC ABI kapsam dışı (açık hata).
  - Constructor/destructor: ilk sürümde açık `init`/`deinit` metodları.
- [ ] **4.5** Interface: bildirim, `class/struct implements`, `iface` değeri = `{data: *void, itable: *T}`;
  statik tipten arayüze dönüşüm itable'ı derleme zamanında üretir; çağrı = itable'dan dolaylı çağrı.
- [ ] **4.6** Modül yükleme: AST klonlama yerine modül başına `Program` + sembol tablosu; mangling tek yerde.
- [ ] **4.7** Codegen'i HIR'a taşı; `resolve.cpp`/`print.cpp`/`expr.cpp`'deki tip çözümleme mantığı
  sema'ya gider. Eski AST→LLVM yolu silinir (yan yana yaşatılmaz).

**Kabul:** class/interface/vtable içeren küçük programlar çalışır; `@repr(c)` class'ın nesne düzeni
ve vtable'ı clang ile birebir uyuşur (tek kalıtım).

## Faz 5 — Codegen kalitesi

- [ ] **5.1** Alloca'lar fonksiyon entry bloğuna (`mem2reg` için).
- [ ] **5.2** `PassBuilder` ile `buildPerModuleDefaultPipeline(level)`; `CodeGenOptLevel` bağla; `--cpu`.
- [ ] **5.3** Syscall hedef OS ayrımı: Darwin x86_64 (`0x2000000` + numara), Darwin aarch64
  (`x16`, `svc #0x80`); desteklenmeyen OS/arch için açık hata.
- [ ] **5.4** Debug info (`DIBuilder`): compile unit, subprogram, satır konumları, struct/class tipleri.
- [ ] **5.5** Pointer↔integer dönüşümleri yalnızca `syscall` sınırında ve açık `as`'ta.

## Faz 6 — Düşük seviye primitifler (thread-agnostic)

Stdlib'in ihtiyacı olan minimum; hiçbiri "thread" kavramı taşımaz:

- [ ] **6.1** Atomic builtin'ler: `atomic_load/store/xchg/cas/add` + memory order, `fence`
  (LLVM `load atomic`/`atomicrmw`/`cmpxchg`/`fence`).
- [ ] **6.2** Fonksiyon pointer'ı: `&fn_name`, dolaylı çağrı, `fn(A) -> R` tipi.
- [ ] **6.3** Generic inline asm builtin'i (Linux ham `clone` trampoline'i gibi durumlar için; opsiyonel).
- [ ] **6.4** Derleme zamanı hedef sabitleri `os`, `arch` (Faz 3'te yerleşik tanım olarak zaten var).

## Faz 7 — xlang stdlib (compiler hedefi tamamlandıktan sonra)

İlke: compiler thread/scheduler/print/alloc bilmez; hepsi xlang ile yazılır ve platform
API'sini kendisi çağırır.

```
kullanıcı kodu → std/{thread,sync,sched,io,mem} → std/sys/{linux,macos,windows} → syscall / extern
```

- [ ] **7.1** `std/sys/*`: Linux'ta ham syscall (`clone`, `futex`, `mmap`, `write`); macOS/Windows'ta
  kararlı ABI olmadığından `declare fn pthread_*` / `CreateThread` (libSystem/kernel32). Yine stdlib yapar.
- [ ] **7.2** `std/mem`, `std/io`: `alloc/free`, `print` (compiler'a gömülü değil).
- [ ] **7.3** `std/thread`: `thread_start(entry: fn(*void), arg: *void)`, `thread_join`. Argümanlar
  heap env'de (global yok → veri yarışı yok). v1 platform thread API'si, v2 Linux ham `clone`.
- [ ] **7.4** `std/sync`: atomic + futex/pthread üstünde mutex/cond/rwlock.
- [ ] **7.5** `std/sched`: iş kuyruğu, worker havuzu, `spawn(fn, arg)`, `wait_all()`.
- [ ] **7.6** xmake `std` hedefi: stdlib'i derleyip compiler modül arama yoluna kurar.
- [ ] **7.7** Compiler'dan kaldır: `spawn`/`go`, `print` özel işleme, `malloc/free/printf` prelude,
  `needs_heap_`/`needs_strings_`; `new/delete` ya stdlib sembolüne iner ya da kalkar.
  Eski example/lib'ler yeni dile göre yeniden yazılır (bu aşamada).

**Kabul:** compiler kaynağında `spawn`, `thread`, `pthread`, `printf`, `malloc` geçmez (link
bayrakları hariç); Linux ve macOS'ta aynı kullanıcı kodu farklı `sys/*` ile çalışır.

## Faz 8 — Ölçekleme (ihtiyaç doğunca)

- [ ] **8.1** Modül başına derleme: modül başına LLVM modülü/`.o`, arayüz dosyası, bağımlılık grafiği.
- [ ] **8.2** İçerik hash'li incremental/cache.
- [ ] **8.3** (Koşullu) MIR: yalnızca ikinci backend / dile özgü CFG analizi gerekirse.

---

## Sıra

```
Faz 0 ─► Faz 1 ─► Faz 2 ─► Faz 3 ─► Faz 4 ─► Faz 5 ─► Faz 6 ─► Faz 7 ─► Faz 8
 güvenlik  syntax   tipler    #if      sema/    codegen  primitif  stdlib   ölçek
                    layout    CLI      HIR      kalite
```

- Geriye uyumluluk olmadığı için ara geçiş katmanı yazılmaz; her faz kendi yüzeyini doğrudan değiştirir.
- Faz 5.1–5.3 küçük ve bağımsız; Faz 0'dan sonra herhangi bir noktada öne çekilebilir.
- Faz 2 (layout) ve Faz 4.4–4.5 (class/interface) birbirine bağlı: class düzeni Faz 2 motorunu kullanır.

## PR bölünmesi

1. `verifyModule` + yeni test iskeleti (0)
2. Lexer/parser yeni sözdizimi + AST/Type yeniden tasarımı (1.1–1.3)
3. Diagnostics (1.4)
4. Tip modeli + işaretli/işaretsiz lowering (2.1–2.2)
5. Layout motoru + `@repr(c)` + `size_of/offset_of/static_assert` + clang karşılaştırma testi (2.3–2.5)
6. `#if` + yerleşik tanımlar (3.1–3.3, 3.5)
7. CLI bayrakları `-D/-U/--target/-O/-g/--emit/--print-cfg` + `define` sabitleri (3.4, 3.6)
7b. Macro genişletme + `type` takma adı (3.7–3.8)
8. Sema + scope + modül yükleme (4.1–4.2, 4.6)
9. HIR + codegen taşıma (4.3, 4.7)
10. Class (4.4), Interface (4.5)
11. Alloca/PassBuilder/Darwin syscall/debug info (5)
12. Atomic + fnptr + asm (6)
13. Stdlib (7.1–7.6), ardından compiler'dan kaldırma (7.7)
