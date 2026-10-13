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
