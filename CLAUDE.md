# xlang — Claude için proje notları

## Plan dosyaları (önce bunlar okunur)

| Dosya | İçerik |
|-------|--------|
| `PLAN.md` | Compiler planı: kurallar, kararlar, fazlar, PR bölünmesi |
| `PLAN.eg.md` | Plan bitince dilin nasıl görüneceği: özellik başına açıklama, ASCII tablo, xlang kod örneği |

### Senkron kuralı

- Plan değişince **dil yüzeyi** (sözdizimi, tipler, özellikler, CLI bayrakları, import biçimleri,
  öznitelikler, builtin'ler) etkileniyorsa **aynı değişiklikte `PLAN.eg.md` de güncellenir**.
- Compiler iç yapısı (IR, pass, codegen) değişiyorsa yalnızca `PLAN.md` güncellenir.
- Kullanıcı genelde `PLAN.eg.md` üzerinden plan güncellemesi ister; yeni özellik eklerken önce
  `PLAN.eg.md`'ye örnek (açıklama + ASCII tablo + `xlang` kod bloğu) yazılır, sonra `PLAN.md`'ye madde eklenir.
- `PLAN.eg.md` içindeki her özellik: 1 başlık, kısa açıklama, tablo (gerekirse), `xlang` kod örneği.

## Çalışma kuralları

- **Geriye/ileriye uyumluluk yok.** Compiler hiç kullanılmadı; sözdizimi, AST, CLI, IR kökten
  değiştirilebilir. Uyumluluk katmanı yazma.
- **`examples/`, `test/xlang/` ve stdlib/runtime `.xlang` dosyalarını okuma, onlara uydurma.**
  Kırılabilirler; compiler hedefine ulaşınca yeniden yazılır.
- **`.cursor/` altındaki kuralları/planları okuma.**
- Compiler yalnızca dili ve CPU primitiflerini (syscall, atomic, fonksiyon pointer'ı, inline asm) bilir.
  OS/thread/scheduler/print/malloc compiler'da olmaz; xlang stdlib'dir.
- Kullanıcının söylediği dışında özellik/tasarım uydurma; karar gerekiyorsa `PLAN.md` "Karar bekleyen"
  bölümüne yaz ya da sor.
- Dosya düzenlemelerinde varsayılan araçları (Read/Edit/Write) kullan; toplu regex/python/sed yalnızca gerekirse.

## Dil kararları (özet, ayrıntı `PLAN.eg.md`'de)

- Fonksiyon: `fn ad(a: i32) -> i32 { }` (`->` yoksa `void`).
- Tipler: `i8 i16 i32 i64 isize`, `ui8 ui16 ui32 ui64 usize`, `f32 f64`, `bool`, `void`, `*T`, `fn(A) -> R`.
- Bellek düzeni: varsayılan xlang düzeni; `@repr(c)` C/C++ düzeni.
- `class` (tek kalıtım, vptr) ve `interface` (fat pointer `{data, itable}`) compiler'da.
- Koşullu derleme: `#if / #elif / #else { }` (yapısal, makro yok); CLI: `-D`, `-U`, `--target`, `-O`, `-g`.
- Tip takma adı: `type Ad = Tip` (şeffaf). Macro: `define N = sabit`, `define f(x: expr) { ... }`, çağrı `f!(...)`;
  gövde süslü parantezli, `\` ve `#`/`##` yok, türlü parametre, hijyenik, `stringify!`.
- Import: `from "m" import x` ve `import x from "m"` ikisi de geçerli.
- Syscall: builtin `syscall(nr, args...)`.

## Build

- Build sistemi xmake: `xmake build`; çıktı `build/xlang`.
- `xlang build <dosya> --emit-ir --keep-ir` ara IR dosyasını bırakır.
