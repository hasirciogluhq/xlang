## Faz 5 — Codegen kalitesi

- [ ] **5.1** Alloca'lar fonksiyon entry bloğuna (`mem2reg` için).
- [ ] **5.2** `PassBuilder` ile `buildPerModuleDefaultPipeline(level)`; `CodeGenOptLevel` bağla; `--cpu`.
- [ ] **5.3** Syscall hedef OS ayrımı (`src/codegen/target.cpp`): compiler `syscall(nr, args...)`'ı
  hedefin (OS + arch) çağrı kuralına çevirir; hangi syscall olduğunu bilmez. **Argüman sayısı dinamiktir:**
  compiler kaç argüman verildiyse o kadarını iletir (3 verildiyse 3), eksikleri sıfırlamaz/doldurmaz.
  Her hedef için tabloda bir **en fazla argüman sayısı** vardır; kod üretilmeden önce `--target`'ın OS/arch'ına
  göre denetlenir ve aşılırsa derleme hatası verilir (hata mesajı hedefi ve sınırı söyler).

  | Hedef | Komut | Numara | Argümanlar | Not |
  |-------|-------|--------|------------|-----|
  | Linux x86_64 | `syscall` | `rax` | `rdi rsi rdx r10 r8 r9` | |
  | Linux aarch64 | `svc #0` | `x8` | `x0..x5` | |
  | macOS x86_64 | `syscall` | `rax` (`0x2000000 +` sınıf) | `rdi rsi rdx r10 r8 r9` | BSD sınıfı |
  | macOS aarch64 | `svc #0x80` | `x16` | `x0..x5` | |
  | Windows x86_64 (NT) | `syscall` | `eax` (SSN) | `r10 rdx r8 r9`, fazlası stack (`[rsp+0x28..]`) | 4'ten fazla argümanda compiler stack'i hazırlar |

  Windows NT syscall numaraları (SSN) sürüme göre değişir; numara tablosu stdlib'dedir ve `-D` ile seçilir
  (compiler tabloyu bilmez). Desteklenmeyen OS/arch için açık hata. En fazla argüman sayısı yalnızca bu
  tablodan gelir (register ile geçenler tablodaki sütunlardır; Windows'ta stack aktarımıyla daha yüksek);
  başka yerde sabit bir sayı yazılmaz.
  **Dönüş sözleşmesi (hedefler arası tek biçim):** `syscall` `i64` döner ve hatayı **negatif `-errno`**
  olarak bildirir. Linux zaten böyledir; macOS carry bayrağını set edip pozitif errno döndürdüğü için
  compiler carry set ise sonucu `-errno`'ya çevirir (dönüş değeri ABI'sidir, hangi syscall olduğunu bilmek
  gerekmez). Windows'ta NTSTATUS (`eax`) olduğu gibi dönüş değeridir, `i64`'e işaret genişletilir.
- [ ] **5.4** Debug info (`DIBuilder`): compile unit, subprogram, satır konumları, struct/class tipleri.
- [ ] **5.5** Pointer↔integer dönüşümleri yalnızca `syscall` sınırında ve açık `as`'ta.
- [ ] **5.6** Çıktı biçimleri: `--emit=obj` (nesne), `ir` (`.ll`, `clang++` ile derlenebilir; hedef triple
  ve data layout yazılı), `bc` (bitcode). Kaynak: yalnızca LLVM Module; farklı yazıcılar.
- [ ] **5.7** `--emit=header`: `export(c)` fonksiyonları ve `@repr(c)` tipler için `extern "C"` korumalı
  C/C++ başlığı (`src/codegen/header.cpp`). Compiler başka bir şey üretmez/bağlamaz.
- [ ] **5.8** `--emit=static` (arşivle) ve `--emit=shared` (paylaşımlı kütüphane): yalnızca kolaylık;
  obj/ir üretimiyle karıştırılmaz. Kullanıcının kendi build script'i (xmake) obj/ir'i alıp kendisi de
  bağlayabilir; compiler bunu zorunlu kılmaz.
