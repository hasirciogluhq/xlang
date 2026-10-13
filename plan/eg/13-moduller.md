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
| `export(c)` | `export` + C ABI'de **mangle edilmemiş** sembol; C/C++ buradan çağırır ([16](16-name-mangling.md#16-name-mangling), [18](18-c-ile-kullanim-ve-cikti-bicimleri.md#18-c-ile-kullanım-ve-çıktı-biçimleri)) |

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
