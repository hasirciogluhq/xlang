## 8. Interface

`interface` yalnızca metod imzaları tanımlar. Struct veya class bir interface'i **implement** edebilir.
Interface değeri `{data, itable}` ikilisidir; nesnenin kendi düzenine **dokunulmaz** (C düzeni bozulmaz).

```xlang
interface Writer {
    fn write(self, buf: *ui8, len: usize) -> isize
}

struct FileOut {
    fd: i32
}

impl Writer for FileOut {
    fn write(self, buf: *ui8, len: usize) -> isize {
        return syscall(1, self.fd, buf, len) as isize     // write
    }
}

fn emit(w: Writer, msg: *ui8, n: usize) -> isize {
    return w.write(msg, n)       // itable üzerinden dolaylı çağrı
}

fn main() -> i32 {
    local f: FileOut = FileOut { fd: 1 }
    emit(f, "hi\n", 3)           // FileOut -> Writer: {data=&f, itable=FileOut:Writer}
    return 0
}
```

```
Writer değeri (fat pointer, 16 B)

  +-----------+-----------+
  | data  *   | itable *  |
  +-----------+-----------+
        |           |
        v           v
   FileOut nesnesi  [ &FileOut.write ]      (derleme zamanında üretilen sabit tablo)
```

| Yapı | Anlam |
|------|-------|
| `interface Ad { fn ... }` | Metod imzaları |
| `impl Ad for Tip { ... }` | Tipin interface'i implement etmesi; **zorunlu**: metodlar tutsa bile `impl` yoksa tip o interface olarak kullanılamaz, dönüşüm hatadır (`E0504`). Yapısal eşleşme yoktur |
| Interface tipli parametre/yerel | `{data, itable}` fat pointer |
| Çağrı | `değer.metod(...)` itable'dan dolaylı |

```xlang
struct Console { fd: i32 }          // write(self, buf, len) metodu var ama impl Writer for Console YOK

fn main() -> i32 {
    local c: Console = Console { fd: 1 }
    emit(c, "hi\n", 3)              // HATA E0504
    return 0
}
```

```
error[E0504]: `Console` `Writer` interface'ini implement etmiyor
  --> app.xl:5
   |
 5 |     emit(c, "hi\n", 3)              <======= `Console` -> `Writer` dönüşümü: `impl` yok
   |
detay:  `emit` parametresi `Writer` interface'idir; `Console` için `impl Writer for Console` bulunamadı.
neden:  Interface'ler nominaldir; metod adları tutsa bile `impl` yazılmadan dönüşüm yapılmaz.
düzelt: `impl Writer for Console { fn write(...) { ... } }` ekle.
```
