## 9. Koşullu derleme

Satır tabanlı direktifler: `#if`, `#elif`, `#else`, `#endif`, `#ifdef`, `#ifndef`, `#error`.
Koşul ifadesi, dilin normal `if` ifadesiyle **aynı** sözdizimi ve aynı operatörlerdir; tek fark derleme
zamanında basit bir yorumlayıcıyla değerlendirilmesidir. Direktif, kendi satırının başında `#` ile yazılır.
Pasif bölge yalnızca **token olarak okunur** (lexer'dan geçer), parse edilmez ve derlenmez.

### `#endif` ne zaman gerekli?

- **Gövde aynı satırdaysa `#endif` yazılmaz.** Direktif tek satırı kontrol eder.
- **Gövde alt satıra inerse `#endif` zorunludur.**

```xlang
#if os == "linux" fn pageSize() -> usize { return 4096 }     // aynı satır: #endif YOK

#if os == "macos" && arch == "aarch64"                         // gövde alt satırda: #endif GEREKLİ
    fn pageSize() -> usize { return 16384 }
    fn cacheLine() -> usize { return 128 }
#elif os == "windows"
    fn pageSize() -> usize { return 4096 }
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
| Yerleşik | `os`, `arch`, `pointerWidth`, `endian`, `debug`, `optLevel` | hedef/derleme ayarından |
| CLI | `-D ENABLE_STATS`, `-D LEVEL=3`, `-D NAME="x"` | bool / tamsayı / string |
| Dosyada | `define LEVEL = 3` ([9.2](09.2-define-ve-macro.md#92-define-ve-macro)) | sabit; CLI `-D` bunun yerine geçer |

| İfade | Anlam |
|-------|-------|
| `defined(NAME)` | CLI/yerleşik olarak tanımlı mı |
| `NAME == değer`, `!=`, `<`, `<=`, `>`, `>=` | karşılaştırma |
| `&&`, `\|\|`, `!` | mantıksal |
| `#error "msg"` | dal seçilirse derleme hatası |

Koşullar derleme zamanı sabitidir; çalışma zamanı değişkeni kullanılamaz.
