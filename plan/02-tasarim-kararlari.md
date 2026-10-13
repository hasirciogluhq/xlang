## 2. Tasarım kararları (özet)

| Konu | Karar | Neden |
|------|-------|-------|
| Syscall | Builtin `syscall(nr, args...)` (yapıldı); argüman sayısı **dinamik**: compiler verilen sayıyı olduğu gibi iletir, sabit sınır yok. Derleme hedefinin OS/arch'ına göre izin verilen en fazla argüman sayısı aşılırsa **derleme hatası** | Compiler hangi syscall olduğunu bilmez; sınır OS'a göre değişir |
| Fonksiyon sözdizimi | `fn name(a: i32) -> i32 { }`; `->` yoksa dönüş `void` | İstek: `=>` değil `->` |
| Tamsayı tipleri | `i8 i16 i32 i64 isize` / `ui8 ui16 ui32 ui64 usize` | İşaretli/işaretsiz ayrımı; kullanıcı yazımı `ui32` |
| Kayan nokta | `f32`, `f64` | |
| Diğer tipler | `bool`, `void`, `*T` (pointer), `fn(A) -> R` (fonksiyon pointer'ı), `[T; N]` (sabit boyutlu dizi), `struct`, `class`, `interface`, `enum` | |
| Örtük dönüşüm | Aynı işaretlilikte tamsayı genişlemesi örtük; işaretli tamsayı → `f32/f64` örtük ama `W0601` uyarısı (`@allow(W0601)`/`as` ile susar); işaretlilik değiştiren (`ui ↔ i`, `ui → f`; float işaretli sayılır) örtük dönüşüm **hata** `E0601`, yalnızca `unsafe { }` bloğunda geçer (aynı ya da daha geniş hedef); diğer her şey açık `as` | Sessiz işaret/daralma hataları olmasın; bilerek yapılan açıkça işaretlensin |
| `unsafe` bloğu | `unsafe { ... }`: şu an yalnızca işaretlilik değiştiren (`ui ↔ i`) örtük dönüşümü açar; başka kontrolü (`--safety`, `*T`, `ref T`) etkilemez | İstek: Rust benzeri bilinçli kaçış, dar kapsam |
| Taşma | İşaretli ve işaretsiz tamsayılar sarar (two's complement), bölme sıfıra = `panic` (kayıtlı `panic` yoksa trap) | Tanımlı davranış |
| Bellek düzeni | Varsayılan: **xlang düzeni** (derleyici alan sırasını/padding'i seçebilir). `@repr(c)` ile işaretli struct/class **C/C++ düzenine** birebir uyar | İstek |
| Class | Tek kalıtım, sanal metod, vptr; `@repr(c)` ile Itanium C++ ABI düzeni (tek kalıtım) | İstek: compiler class bilsin |
| Interface | Fat pointer `{data, itable}` (Go/Rust tarzı); struct ve class implement edebilir | Düzeni (C uyumu) bozmaz |
| Import | `import "m" as a`, `from "m" import x` **ve** `import x from "m"` (ikisi de geçerli, aynı AST) | İstek: iki sıra da desteklensin |
| Koşullu derleme | Satır tabanlı direktifler `#if/#elif/#else/#endif/#ifdef/#ifndef/#error`; koşul, dilin normal `if` ifadesiyle aynı sözdizimi ve operatörler (`defined(X)`, `!defined(X)`, `&& \|\| !`, karşılaştırma), derleme zamanında basit yorumlayıcıyla değerlendirilir. **Gövde aynı satırdaysa `#endif` gerekmez**, alt satıra inerse `#endif` zorunlu. Pasif bölge yalnızca lex edilir | İstek |
| Dış kütüphane / stdlib | Dil dışarıdan kütüphane almaz: libc, libSystem, kernel32, pthread kütüphanesi yok. Stdlib her OS'un çekirdek ABI'sini `#if os` ile ham `syscall` üzerinden çağırır. Stdlib isteğe bağlı: `--no-std`, `--std=<yol>`; varsayılan stdlib dile gömülü, içinde Go benzeri scheduler var | İstek |
| libc | **YASAK.** Hiçbir koşulda kullanılmaz. xl'in kendi stdlib'i vardır | İstek |
| Builtin fonksiyonlar | Global, compiler'ın **gövdesini kendisinin sağladığı** fonksiyonlar. **Liste:** `syscall` (öneksiz), atomic'ler, `fence` ve inline asm (`__builtin_` önekli). `memcpy/memset/memmove/memcmp` (LLVM'in örtük çağrıları için C adıyla bulunmalı) ve `malloc/free`/allocator builtin değildir, stdlib'dendir (`std/memory`, `std/mem`). Compiler doğrudan desteklemeyecekse builtin olmaz. İki yazım: `__builtin_xx` (atomic'ler, `fence`, `__builtin_asm`) ve öneksiz (`syscall`, `size_of` ailesi, `static_assert`); `syscall` her zaman doğrudan `syscall` | İstek |
| Tanılar | Compiler kısıtlamaz, uyarır: her uyarı `ne + detay + neden + nasıl düzeltilir`; kodlu (`W0201`...), `--allow/--deny/@allow` ile susturulur veya hataya çevrilir, `--explain` | İstek |
| Düzen geçmişi | Varsayılan düzen **sürümlü şema** (v1, v2, v3...), eski şemalar donmuş ve compiler'da kalır; `.o/.a/.bc` düzen kaydı taşır; `xl.layout` proje geçmişi; bilerek karıştırma (`@layout(shuffle)`) kayıtta işaretlenir; karıştırılmamışsa düzen yeniden türetilir | İstek: önceki derleyici çıktılarının düzenini bilme |
| `declare` | Tek önek; her bildirimin başına konur: `declare fn/global/struct/class/interface/type`. Global için ayrı sözdizimi yok. Öznitelikler normal bildirimle aynı (`@repr(c)`, `@packed`, `@align`, `@mangle`); `@repr(c)` `declare`'de **zorunlu değil**; düzen yoksa kayıt → geçmiş → C varsayımı + uyarı. Dış tarafın xl/C++/Rust/`.o`/statik olması kısıtlanmaz | İstek |
| Modül yolları | `"std"` → `std.xl` ya da `std/index.xl`; `"std/memory"` aynı kuralla. Giriş dosyası `index.xl` | İstek: `import "std"` hepsini versin |
| Çift import / çift export | Aynı ada sahip iki bağlama hatadır, **import/export satırında** raporlanır: `import * from a` + `import * from b` (ortak ad), çift adlandırılmış import, yerel tanımla çakışma. `import * as aa from a` + `import * as bb from b` çalışır. Aynı varlığın iki yoldan gelmesi hata değil ama **detaylı uyarı** (`W0401`) + log | İstek |
| Yeniden dışa aktarma | `export * from "m"`, `export * as ns from "m"`, `export { a, b as c } from "m"`, `export * from "m" except a, b`, `#if` içinde koşullu | İstek: JS benzeri ama daha geniş |
| Kaynak uzantısı | `.xl` | İstek |
| Görünürlük | **Varsayılan export yok.** `export` yazılmayan sembole başka modül erişemez; link'te de internal. `export(c)` = `export` + mangle edilmemiş C ABI sembolü | İstek: C-like export, kendi modülleri arası kullanım |
| Name mangling | Varsayılan açık (`xl` şeması). Proje (`--mangle=`), dosya (`@file(mangle = ...)`), fonksiyon (`@mangle(...)`) seviyesinde override; öncelik fn > dosya > proje | İstek |
| Bellek güvenliği | Ham `*T` C gibi kontrolsüz kalır; ek `ref T` = `{ptr, gen}` nesil kontrollü güvenli referans (use-after-free'de `panic`). Borrow checker / lifetime anotasyonu yok. `--safety=checked\|unchecked`, `@unchecked fn` | İstek: Rust benzeri koruma, dil kalıbını bozmadan |
| C++ birlikte kullanım | Compiler yalnızca `obj` / `ir` (`.ll`) / `bc` / `header` / `exe` / `static` / `shared` üretir; bağlama ve build'i kullanıcının script'i (xmake vb.) yürütür | İstek: compiler karar vermesin |
| Tip takma adı | `type Ad = Tip` (şeffaf alias, yeni tip yaratmaz), `export type` | İstek: typedef benzeri |
| define / macro | `define N = sabit` ve `define f(x: expr) { ... }`, çağrı `f!(...)`; gövde süslü parantezli, `\` yok, `#`/`##` yok, türlü parametre (`expr/type/ident/block/items`), hijyenik; macro bildirim ve `import` üretebilir, `export define` ile import edilebilir | İstek: C önişlemcisinin pisliği olmadan macro |
| CLI tanımları | `-D/--define NAME[=DEĞER]`, `-U/--undef NAME`, + yerleşik `os`, `arch`, ... | İstek |
| Thread/scheduler/IO/alloc | xlang stdlib; compiler görmez (Faz 7). İstisna: `register` ile kayıtlı roller (aşağıda) | Compiler yalnızca düşük seviye bilir |
| `register` | `register(rol) bildirim` / `register bildirim`: `declare` gibi tek önek, ters yönde: bildirilen ad modül derlemeye girince **global** olur (import gerekmez, `export` da sayılır). Roller: `panic`, `allocator`; rol başına en fazla bir kayıt. Gölgeleme: yerel tanım/import kayıtlı adı gölgeler (`W0402`). Compiler stdlib'i ayırt etmez | İstek: stdlib'in `panic`'i import'suz görünsün, compiler otomatik çağırsın |
| Panic / assert | `register(panic) fn panic(msg: *ui8)` `std/runtime`'da; compiler'ın eklediği hata yolları (sıfıra bölme, `ref` nesil uyuşmazlığı, dizi sınırı) onu çağırır, kayıtlı yoksa trap. `assert!` `std/assert`'te macro | İstek |
| Allocator / `new` | `new T { }` (kayıtlı allocator) ve `new(a) T { }` (açık). Allocator = `alloc` + `free` içeren grup (`std/mem`'de `Allocator` interface'i); bırakma aynı gruptan. Kayıtlı allocator `main`'de `set_allocator` ile **bir kez** çalışma zamanında değiştirilebilir | İstek |
| Enum | `enum Ad: T { A, B = 5 }`; `T` sabit değer alabilen herhangi bir tip (tamsayı, `f32/f64`, `bool`, `*ui8` string); yazılmazsa `i32`; erişim `Ad.A`; dönüşüm açık `as` | İstek |
| Dizi | `[T; N]`; indeksleme `a[i]` (dizi ve `*T`); dizide sınır kontrolü `--safety=checked` (varsayılan) altında `panic`; değer tipi; literal `[a, b]` ve `[v; N]` | İstek: C struct uyumu + güvenli indeksleme |
| Pass manager | New PassManager (`PassBuilder`) | Legacy PM deprecated |
| Derleme modeli | Whole-program, tek LLVM modülü; ayrı derleme en sona | Basit, doğru |
| HIR | Eklenecek | Sema çıktısı; codegen yalnızca tipli ağacı görür |
| LIR | Eklenmeyecek | LLVM IR zaten LIR; ikinci düşük seviye IR kazanç sağlamaz. Koşul: ikinci backend ya da dile özgü CFG analizi → o zaman MIR |
