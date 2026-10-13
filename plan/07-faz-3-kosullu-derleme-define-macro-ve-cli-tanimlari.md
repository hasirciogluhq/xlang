## Faz 3 — Koşullu derleme, define/macro ve CLI tanımları

- [ ] **3.1** Önişleme (`src/lang/preprocess/`): lexer'dan sonra, parser'dan önce, token akışı üzerinde.
  Direktifler satır başında `#if`, `#elif`, `#else`, `#endif`, `#ifdef`, `#ifndef`, `#error`.
  - **Tek satır biçimi:** koşul ve gövde aynı satırdaysa `#endif` yoktur (gövde o satırın kalanıdır).
  - **Blok biçimi:** gövde alt satırdan başlıyorsa `#endif` zorunlu; `#elif`/`#else` yalnızca blok biçiminde.
  - Pasif bölge yalnızca lex edilir; parse/sema/codegen'e girmez. İç içe direktifler desteklenir.
  - Koşul/gövde sınırı: koşul ifadesinin bittiği token; belirsizlikte koşul parantezlenir.
  - Item, statement, struct/class alanı ve `export ... from` seviyesinde çalışır (token akışı olduğu için
    konumdan bağımsız).
- [ ] **3.2** Koşul ifadesi: dilin normal ifade sözdizimi ve operatörleri, derleme zamanı sabit ifade olarak
  basit yorumlayıcıyla değerlendirilir: `&& || !`, `== !=`, `< <= > >=`, tamsayı/string/bool sabitleri,
  `defined(NAME)` / `!defined(NAME)`; `#ifdef NAME` ve `#ifndef NAME` bunların kısaltmasıdır.
- [ ] **3.3** Yerleşik tanımlar (hedeften türer): `os` (`"linux"|"macos"|"windows"`), `arch`
  (`"x86_64"|"aarch64"`), `pointerWidth` (`32|64`), `endian` (`"little"|"big"`), `debug` (bool),
  `optLevel` (0..3).
- [ ] **3.4** CLI bayrakları (`src/cli`):

  | Bayrak | Anlam |
  |--------|-------|
  | `-D NAME`, `--define NAME` | `NAME = true` |
  | `-D NAME=DEĞER`, `--define NAME=DEĞER` | `NAME` tamsayı/string/bool değeri |
  | `-U NAME`, `--undef NAME` | Tanımı (yerleşik dahil olmayan) kaldırır |
  | `--target <triple>` | Hedef triple; `os/arch/pointerWidth` buradan türer |
  | `-O0..-O3` | Optimizasyon seviyesi (`optLevel`) |
  | `-g` | Debug info (`debug = true`) |
  | `--emit=ir\|bc\|obj\|hir\|header\|exe\|static\|shared` | Çıktı biçimi/aşaması; varsayılan `exe` |
  | `--mangle=xl\|none` | Proje seviyesi mangling varsayılanı |
  | `--safety=checked\|unchecked` | `ref T` kontrolleri |
  | `--print-cfg` | Çözülmüş tüm tanımları yazdır |
  | `--cpu <name>` | Hedef CPU (varsayılan `generic`) |
  | `--no-std` / `--std=<yol>` / `--std-features=...` | Stdlib seçimi (varsayılan: gömülü stdlib) |

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
  - `#`/`##` ve satır devam karakteri sözdiziminde **yoktur** (`stringify!` da yoktur).
- [ ] **3.8** `type` takma adları sema'da çözülür (Faz 4.1 scope'unda), HIR'da hedef tipe indirgenir;
  döngüsel takma ad hatası.

**Kabul:** `-D DISABLE_X` ile bir item/alan/statement elenir; `--print-cfg` doğru değerleri gösterir.
