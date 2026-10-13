## 12. Atomic ve fonksiyon pointer'ı

Thread/mutex gibi şeyler compiler'da yoktur; yazılabilmeleri için yalnızca atomic işlemler,
fonksiyon pointer'ı ve [inline asm](12.1-inline-asm.md#121-inline-asm) vardır. Atomic fonksiyonlar compiler
builtin'idir ve **`__builtin_` önekiyle** global olarak bulunur. `mutex` stdlib'dedir (`std/sync`),
altında bu atomic'leri kullanır.

```xlang
fn spinLock(flag: *ui32) {
    while __builtin_atomicCas(flag, 0, 1, MemoryOrder.seqCst) != 0 { }
}

fn spinUnlock(flag: *ui32) {
    __builtin_atomicStore(flag, 0, MemoryOrder.release)
}

fn callTwice(f: fn(i32) -> i32, v: i32) -> i32 {
    return f(f(v))
}

fn inc(x: i32) -> i32 { return x + 1 }

fn main() -> i32 {
    return callTwice(&inc, 0)       // 2
}
```

| Builtin | Anlam |
|---------|-------|
| `__builtin_atomicLoad(p, order)` | Atomik okuma |
| `__builtin_atomicStore(p, v, order)` | Atomik yazma |
| `__builtin_atomicXchg(p, v, order)` | Değiştir, eskiyi döndür |
| `__builtin_atomicCas(p, expected, new, order)` | Karşılaştır-değiştir, eski değeri döndür |
| `__builtin_atomicAdd(p, v, order)` | Ekle, eskiyi döndür |
| `__builtin_fence(order)` | Bellek bariyeri |
| `&fnName` | Fonksiyon adresi (`fn(...) -> R` tipli) |

`order`, builtin bir enum'dur ([6.1](06.1-enum.md#61-enum)); import gerekmez:

```xlang
enum MemoryOrder: ui8 { relaxed, acquire, release, acqRel, seqCst }     // builtin
```

Argüman `MemoryOrder.seqCst` gibi **derleme zamanı sabiti** olmalıdır. Atomic'ler `i8..i64`, `ui8..ui64`,
`isize`, `usize`, `*T` ve `bool` üzerinde çalışır; diğer tipler derleme hatasıdır.
