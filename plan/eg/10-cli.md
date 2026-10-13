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
| `--emit=ir\|bc\|obj\|hir\|header\|exe\|static\|shared` | Çıktı biçimi ([18](18-c-ile-kullanim-ve-cikti-bicimleri.md#18-c-ile-kullanım-ve-çıktı-biçimleri)) |
| `--mangle=xl\|none` | Proje seviyesi mangling varsayılanı ([16](16-name-mangling.md#16-name-mangling)) |
| `--safety=checked\|unchecked` | `ref T` nesil kontrolleri ve dizi sınır kontrolleri açık/kapalı; varsayılan `checked` ([17](17-guvenli-referans.md#17-güvenli-referans-ref-t), [6](06-struct-ve-bellek-duzeni.md#sabit-boyutlu-dizi-t-n)) |
| `--cpu <ad>` | Hedef CPU (varsayılan `generic`) |
| `--no-std` / `--std=<yol>` | Stdlib yok / başka stdlib ([15](15-stdlib-ile-thread.md#15-stdlib-ile-thread)) |
| `--print-cfg` | Etkin tanımları yazdırır |
