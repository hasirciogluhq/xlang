## 18. C++ ile kullanım ve çıktı biçimleri

Compiler istenen çıktıyı **kendisi** üretir ve gerekiyorsa işi bitirmek için diğer araçları (LLVM, linker,
arşivleyici) çağırır. `--emit` verilmezse varsayılan `exe`'dir: hat sonuna kadar çalışır ve çalıştırılabiliri
verir. `ir`/`bc`/`obj`/`header` istenirse üretir ve orada durur. Compiler yalnızca kendi nesnesini bağlar
(libc ya da dış kütüphane eklemez); bir C++ projesine bağlamayı kullanıcının build script'i (xmake vb.)
yürütür, bunun için `--emit=obj` ya da `ir` alınır.

### Çıktı biçimleri (`--emit`)

| Değer | Dosya | Ne için | Compiler başka araç çağırır mı |
|-------|-------|---------|-------------------------------|
| `exe` (**varsayılan**) | çalıştırılabilir | tek başına program | evet: linker |
| `obj` | `.o` / `.obj` | Doğrudan linkere | hayır, durur |
| `ir` | `.ll` | `clang++` ile derlenebilen LLVM IR (metin) | hayır, durur |
| `bc` | `.bc` | LLVM bitcode | hayır, durur |
| `header` | `.h` | `export(c)` fonksiyonlarının ve `@repr(c)` tiplerin C/C++ başlığı | hayır, durur |
| `static` | `.a` / `.lib` | statik kütüphane (nesneleri arşivler) | evet: arşivleyici |
| `shared` | `.so` / `.dylib` / `.dll` | dinamik kütüphane | evet: linker |

`obj`/`ir`/`bc`/`header` **ara ürünlerdir**: compiler üretir, bırakır. `exe`/`static`/`shared` bitmiş
üründür: compiler nesneyi üretir ve gerekli aracı çağırarak sonuna kadar götürür.

### C++ tarafından kullanılacak API

```xlang
// geom.xl
from "std/math" import sqrt

@repr(c)
export struct Vec2 { x: f64, y: f64 }

export(c) fn vec2Len(v: *Vec2) -> f64 {
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
double vec2Len(const Vec2* v);
#ifdef __cplusplus
}
#endif
```

```cpp
// main.cpp
#include "geom.h"
int main() { Vec2 v{3.0, 4.0}; return vec2Len(&v) == 5.0 ? 0 : 1; }
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
| C++ projesine bağlama | Kullanıcının toolchain'i; bunun için `--emit=obj` alınır |
| Yalnızca xl programı/kütüphanesi | `xlang build` (varsayılan `exe`, ya da `static`/`shared`): compiler linker'ı kendisi çağırır |

`export(c)` imzalarında yalnızca C uyumlu tipler (`i8..i64`, `ui8..ui64`, `f32/f64`, `bool`, `*T`, `@repr(c)` struct,
fonksiyon pointer'ı, tamsayı alt tipli enum) geçebilir; `class` ve `interface` bu imzada derleme hatasıdır.
