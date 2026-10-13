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
