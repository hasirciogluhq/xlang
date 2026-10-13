## 4. Değişkenler ve kontrol akışı

```xlang
fn main() -> i32 {
    local i: i32 = 0
    local sum: i64 = 0

    while i < 10 {
        if i % 2 == 0 {
            sum = sum + i
        } else {
            sum = sum - 1
        }
        i = i + 1
    }

    if sum > 0 { return 0 }
    return 1
}
```

| Yapı | Yazım |
|------|-------|
| Yerel değişken | `local ad: tip = değer` (tip çıkarılabilirse atlanır) |
| Koşul | `if c { } else { }` |
| Döngü | `while c { }` |
| Dönüş | `return değer` |

Karşılaştırma işaretlilikten etkilenir: `ui32` için `<` işaretsiz, `i32` için işaretli karşılaştırmadır.

### Global değişkenler

```xlang
define SIZE = 64

global table:   [ui8; SIZE]            // başlangıç değeri yok: sıfırla başlar
global counter: i64 = 0
global limit:   usize = SIZE * 2       // sabit ifade
global banner:  *ui8 = "xlang"
global origin:  Point = Point { x: 0, y: 0 }
global first:   *ui8 = &table[0]       // başka global'in adresi sabittir
```

Global'in başlangıç değeri **derleme zamanı sabit ifadesi**dir (statik başlatma; çalışma zamanı başlatma
sırası yoktur):

| Sabit sayılan | Sabit sayılmayan |
|---------------|------------------|
| Literal'ler, `define` sabitleri, enum üyeleri | Fonksiyon çağrısı |
| `sizeOf` / `alignOf` / `offsetOf` | Başka global'in **değerini** okumak |
| `&global`, `&global[sabit]`, `as` dönüşümleri | Çalışma zamanı değerleri |
| Alanları sabit olan struct/class/dizi literal'i | |
| Global nesnenin interface'e dönüşümü (`itable` sabit) | |
