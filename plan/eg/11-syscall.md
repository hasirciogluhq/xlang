## 11. Syscall

Compiler yalnızca **numarayı ve argümanları CPU trap'ine yerleştirir**; hangi syscall olduğunu bilmez.
Argüman sayısı **dinamiktir**: compiler kaç argüman verildiyse onu olduğu gibi iletir (3 verildiyse 3).
Sabit bir sınır yoktur; izin verilen en fazla argüman sayısı derlenen hedefin OS/arch'ına göre değişir ve
bu sınır aşılırsa **derleme hatası** verilir. Dönüş her zaman `i64`'tür.

```xlang
fn getpid() -> i64 {
    return syscall(39)                    // 0 argüman: yalnızca numara
}

fn write_all(fd: i64, buf: *ui8, n: usize) -> i64 {
    return syscall(1, fd, buf as i64, n as i64)    // 3 argüman iletilir
}
```

```
error: `syscall` çağrısında 8 argüman var; hedef `x86_64-linux` en fazla 6 argüman alıyor
  --> sys.xl:12:12
   |
12 |     return syscall(9, a, b, c, d, e, f, g, h)    <======= 8 argüman; bu hedef için sınır 6
```

| Hedef | Komut | Numara | Argümanlar | Dönüş |
|-------|-------|--------|------------|-------|
| Linux x86_64 | `syscall` | `rax` | `rdi rsi rdx r10 r8 r9` | `rax` |
| Linux aarch64 | `svc #0` | `x8` | `x0 … x5` | `x0` |
| macOS x86_64 | `syscall` | `rax` (+`0x2000000`) | `rdi rsi rdx r10 r8 r9` | `rax` |
| macOS aarch64 | `svc #0x80` | `x16` | `x0 … x5` | `x0` |
| Windows x86_64 (NT) | `syscall` | `eax` (SSN) | `r10 rdx r8 r9`, fazlası stack | `rax` |

Numaralar hedefe özeldir; bunları bir stdlib katmanı `#if os == ...` ile seçer, compiler bilmez.

**Hata bildirimi tek biçimdedir:** `syscall` hatayı her hedefte **negatif `-errno`** olarak döndürür.
Linux zaten böyle yapar. macOS carry bayrağını set edip pozitif errno döndürür; compiler carry set ise
sonucu `-errno`'ya çevirir (bu dönüş değeri ABI'sidir, hangi syscall olduğunu bilmek gerekmez). Windows'ta
NTSTATUS (`eax`) olduğu gibi dönüş değeridir ve `i64`'e işaret genişletilir. Böylece stdlib'in sys katmanı
Linux ve macOS'ta `sonuç < 0` ile aynı biçimde hata kontrolü yapar.
