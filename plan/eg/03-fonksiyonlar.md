## 3. Fonksiyonlar

Fonksiyonlar `fn` ile, dönüş tipi **`->`** ile yazılır. `->` yoksa dönüş `void`'dir.

```xlang
fn add(a: i32, b: i32) -> i32 {
    return a + b
}

fn log_value(v: i32) {          // -> yok = void
    // ...
}

export fn public_api(x: ui64) -> ui64 {   // modül dışına açık
    return x * 2
}

fn apply(f: fn(i32) -> i32, v: i32) -> i32 {   // fonksiyon pointer'ı parametresi
    return f(v)
}
```

| Parça | Yazım |
|-------|-------|
| Parametre | `ad: tip` |
| Dönüş tipi | `-> tip` |
| Dışa açma | `export fn` |
| Fonksiyon pointer tipi | `fn(i32, i32) -> i32` |
| Giriş noktası | `fn main() -> i32` |
