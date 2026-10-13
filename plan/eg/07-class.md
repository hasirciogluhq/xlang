## 7. Class

`class`, struct'ın üstüne **metod, kalıtım ve sanal çağrı** ekler. Tek kalıtım vardır.

```xlang
class Shape {
    id: i32

    virtual fn area(self) -> f64 {
        return 0.0
    }
}

class Circle : Shape {
    radius: f64

    override fn area(self) -> f64 {
        return 3.14159 * self.radius * self.radius
    }
}

fn describe(s: *Shape) -> f64 {
    return s.area()          // sanal çağrı: vtable üzerinden
}
```

| Yapı | Anlam |
|------|-------|
| `class B : A` | `A`'dan tek kalıtım |
| `virtual fn` | Sanal metod (vtable'a girer) |
| `override fn` | Taban sanal metodu geçersiz kılar |
| `self` | Metodun nesnesi (`*Sınıf` olarak) |
| `init` / `deinit` | Açık kurucu/yıkıcı metod (ilk sürümde otomatik çağrı yok) |

Bellek: sanal metodu olan class'ın başında **vptr** vardır.

```
Circle nesnesi (x86_64)

  +--------+-------+-----------+
  | vptr   |  id   |  radius   |
  | 8 B    | 4 B   |  8 B      |   (id sonrası 4 B padding: radius 8'e hizalanır)
  +--------+-------+-----------+
      |
      v   Circle vtable
  +-----------------+
  | offset-to-top   |   (Itanium C++ ABI sırası; @repr(c) class'ta)
  | typeinfo        |
  | &Circle.area    |   <- vptr buraya işaret eder
  +-----------------+
```

`@repr(c) class` C++'taki tek kalıtımlı polimorfik sınıfla aynı düzeni verir (Itanium ABI).
Çoklu kalıtım ve sanal kalıtım desteklenmez (açık hata).
