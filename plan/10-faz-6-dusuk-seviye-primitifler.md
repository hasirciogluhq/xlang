## Faz 6 — Düşük seviye primitifler (thread-agnostic)

Stdlib'in ihtiyacı olan minimum; hiçbiri "thread" kavramı taşımaz:

- [ ] **6.1** Atomic builtin'ler: `__builtin_atomicLoad/Store/Xchg/Cas/Add` + memory order,
  `__builtin_fence` (LLVM `load atomic`/`atomicrmw`/`cmpxchg`/`fence`). Hepsi `__builtin_` önekli, global.
  Memory order, builtin `enum MemoryOrder: ui8 { relaxed, acquire, release, acqRel, seqCst }` tipidir
  (2.6); argüman `MemoryOrder.seqCst` gibi derleme zamanı sabiti olmalıdır. Atomic'lerin kabul ettiği
  tipler: `i8..i64`, `ui8..ui64`, `isize`, `usize`, `*T`, `bool` (diğerleri derleme hatası).
- [ ] **6.2** Fonksiyon pointer'ı: `&fnName`, dolaylı çağrı, `fn(A) -> R` tipi.
- [ ] **6.3** Generic inline asm builtin'i `__builtin_asm("şablon", in/out/inout/clobber/options...)`
  (**zorunlu**; sözdizimi `plan/eg/` §12.1, LLVM inline asm'ye iner, şablonu yorumlamaz). Şablon hedefe
  özeldir ve `#if arch` / `#if os` ile seçilir (farklı OS için farklı şablon yazılabilir). x86'da
  **Intel** lehçesi, aarch64'te standart sözdizimi. Birden fazla `out` desteklenir (aggregate dönüş yok).
  Varsayılan: bellek okunup yazılabilir sayılır; `options("nomem")` gevşetir, `options("noreturn")` bloğun
  geri dönmediğini bildirir. Gerekçe: ham thread oluşturma (Linux `clone`, macOS `bsdthread_create`,
  Windows `NtCreateThreadEx`) sonrası çocuk thread'in yeni stack'te trampoline'e düşmesi ve scheduler'ın
  bağlam değiştirmesi (stack/register kaydet-yükle).
- [ ] **6.4** Derleme zamanı hedef sabitleri `os`, `arch` (Faz 3'te yerleşik tanım olarak zaten var).
- [ ] **6.5** Builtin tablosu: kapalı tablo (ad, imza, biçim `__builtin_xx` | öneksiz). Sema'da global isim
  olarak çözülür; gövdeyi compiler üretir; çakışma kuralı "Karar bekleyen"e bağlı. Tabloda `__builtin_`
  önekli atomic'ler, `fence` (6.1) ve `__builtin_asm` (6.3) ile `MemoryOrder` enum tipi vardır; `sizeOf`
  ailesi (2.4), `nil` sabiti ve `syscall` (5.3) öneksizdir. `syscall` bir builtin'dir: tabloda yer alır, argüman sayısı
  dinamiktir ve hedefe göre denetimi/çağrı kuralı 5.3'te tanımlıdır. `register` ile kaydedilenler bu
  tabloda değildir (4.12).
