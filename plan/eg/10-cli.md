## 10. CLI

```
xlang build main.xl -o app                          # çalıştırılabilir (varsayılan: --emit=exe)
xlang build lib.xl --emit=static -o libgeom.a       # statik kütüphane
xlang build lib.xl --emit=shared -o libgeom.so      # dinamik kütüphane
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
| `--target <triple>` | Hedef platform; `os`, `arch`, `pointerWidth` buradan gelir |
| `-O0` … `-O3` | Optimizasyon seviyesi (`optLevel`) |
| `-g` | Debug info; `debug = true` |
| `--emit=ir\|bc\|obj\|hir\|header\|exe\|static\|shared` | Çıktı biçimi; **varsayılan `exe`** ([18](18-c-ile-kullanim-ve-cikti-bicimleri.md#18-c-ile-kullanım-ve-çıktı-biçimleri)) |
| `--mangle=xl\|none` | Proje seviyesi mangling varsayılanı ([16](16-name-mangling.md#16-name-mangling)) |
| `--safety=checked\|unchecked` | `ref T` nesil kontrolleri ve dizi sınır kontrolleri açık/kapalı; varsayılan `checked` ([17](17-guvenli-referans.md#17-güvenli-referans-ref-t), [6](06-struct-ve-bellek-duzeni.md#sabit-boyutlu-dizi-t-n)) |
| `--cpu <ad>` | Hedef CPU (varsayılan `generic`) |
| `--no-std` / `--std=<yol>` | Stdlib yok / başka stdlib ([15](15-stdlib-ile-thread.md#15-stdlib-ile-thread)) |
| `--print-cfg` | Etkin tanımları yazdırır |

### Frontend komutları (codegen gelene kadar)

```
xlang parse main.xl -D ENABLE_STATS --target aarch64-apple-darwin   # önişle + macro aç + parse
xlang check main.xl                       # + tipler, enum, layout, staticAssert
xlang layout main.xl Header               # offset/boyut/hizalama; --layout-record xl.layout'a yazar
xlang parse main.xl --print-cfg -D LEVEL=3 -O2 -g                   # çözülmüş tüm tanımlar
```

`-D/-U/--target/-O0..3/-g/--cpu/--print-cfg/--mangle/--safety/--no-std/--std/--std-features/--layout-scheme/
--layout-history` üç komutta da geçerlidir.

### `xlang parse` ve tanı bayrakları

```
xlang parse main.xl                       # öğe özeti (tür, ad, konum)
xlang parse main.xl --ast                 # tam ağaç
xlang parse main.xl --allow=W0601 --deny=W0201 --context=3 --diag-log=diag.log --diag-format=json
xlang --explain E0601                     # kodun uzun açıklaması
```

| Bayrak | Anlam |
|--------|-------|
| `--allow=KOD`, `-Wno-KOD` | Uyarıyı sustur (hata kodları susturulamaz) |
| `--deny=KOD` | Uyarıyı hataya çevir |
| `--warn-all` / `--allow-all` | Tüm uyarıları aç / sustur |
| `--context=N`, `--context-errors=N`, `--context-warnings=N` | Bağlam satırı sayısı |
| `--diag-log=<dosya>`, `--diag-format=text\|json`, `--diag-log-level=all\|warn\|error` | Tanı günlüğü ([19](19-tanilar-ve-uyarilar.md#19-tanılar-ve-uyarılar)) |
| `--explain KOD` | Kodun açıklamasını yazdırır |
