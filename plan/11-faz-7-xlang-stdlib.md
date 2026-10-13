## Faz 7 — xlang stdlib (compiler hedefi tamamlandıktan sonra)

İlke: compiler thread/scheduler/print/alloc bilmez; hepsi xlang ile yazılır ve platform
API'sini kendisi çağırır.

```
kullanıcı kodu → std/{thread,sync,sched,io,memory} → std/sys/{linux,macos,windows} (#if os) → syscall
```

- [ ] **7.0** Stdlib modeli: **dış kütüphane yok** (libc, libSystem, kernel32, pthread kütüphanesi kullanılmaz;
  dil dışarıdan kütüphane almaz). Stdlib her OS için o OS'un çekirdek ABI'sini ham `syscall` ile çağırır;
  OS seçimi `#if os == ...` ile yapılır. Compiler yalnızca syscall'ı hedefe çevirir.
  Stdlib **isteğe bağlıdır**, compiler bayraklarıyla yönetilir:

  | Bayrak | Anlam |
  |--------|-------|
  | (varsayılan) | Dile gömülü varsayılan stdlib kullanılır |
  | `--no-std` | Stdlib yok (freestanding); yalnızca dil + global yerleşikler |
  | `--std=<yol>` | Varsayılan yerine başka bir stdlib kök dizini |
  | `--std-features=a,b` | Stdlib özelliklerini aç/kapat (ör. `sched`, `gpu`, `default-allocator`) |
- [ ] **7.1** `std/sys/*`: OS başına ham syscall katmanı, hepsi `#if os == ...` ile seçilir.
  - Linux: `clone/clone3`, `futex`, `mmap/munmap`, `write/read`, `exit`, `epoll_*`.
  - macOS: `bsdthread_create`/`bsdthread_terminate`, `psynch_*` (mutex/cond), `mmap`, `write/read`, `kevent`.
  - Windows: `NtCreateThreadEx`, `NtWaitForSingleObject`/`NtWaitForAlertByThreadId`, `NtAllocateVirtualMemory`,
    `NtWriteFile`/`NtReadFile`; SSN tablosu sürüme göre `-D` ile seçilir.
  - Numara/yapı tabloları xl içinde (compiler görmez). POSIX uyumlu semantik hedeflenir (pthread benzeri
    thread/mutex/cond davranışı), ama pthread kütüphanesi çağrılmaz.
- [ ] **7.1b** Stdlib yerleşimi: `std/index.xl` (giriş; alt modülleri `export * from` / `export * as` ile
  toplar), `std/memory`, `std/mem`, `std/runtime`, `std/assert`, `std/math` (`sqrt` vb.; xl ile yazılır,
  libm yok), `std/cpu`, `std/gpu`, `std/sys/*`. `import "std"` hepsini, `import "std/cpu"` yalnızca ilgili parçayı verir.
- [ ] **7.2** `std/mem`: `interface Allocator { alloc, free }`, varsayılan `Heap` (OS'tan sayfa alan,
  syscall'larla), `register(allocator) global current_allocator: Allocator` (stdlib özelliği
  `default-allocator`), `free(p)` (şimdiki kayıtlı allocator'ın `free`'si), `set_allocator(a)` (bir kez;
  ikinci çağrı `panic`). `std/io`: `print` (compiler'a gömülü değil; global değil, `import` ile gelir).
  `memcpy/memset/memmove/memcmp` `std/memory`'de **xl ile yazılır** ve `export(c)` ile C adıyla verilir
  (LLVM'in örtük çağrıları için; libc yok); global değildir (bkz. kural 7).
- [ ] **7.2b** `std/runtime`: `register(panic) fn panic(msg: *ui8)` (mesajı yazar, süreci sonlandırır);
  stdlib açıkken her zaman derlemeye girer, böylece `panic` her modülde import'suz görünür. `std/assert`:
  `export define assert(cond: expr)` macro'su; gövdesi `panic` çağırır (kayıtlı olduğu için import'suz).
- [ ] **7.3** `std/thread`: `thread_start(entry: fn(*void), arg: *void)`, `thread_join`, `thread_detach`,
  `thread_id`. Doğrudan OS thread'i (7.1'deki ham syscall'lar). Argümanlar heap env'de (global yok →
  veri yarışı yok); yeni thread'in stack'i `mmap`/eşdeğeriyle ayrılır; çocuk girişi 6.3 asm trampoline'i.
- [ ] **7.4** `std/sync`: `__builtin_atomic_*` + OS'un bekleme primitifi (futex / psynch / NtWaitFor...) üstünde
  mutex/cond/rwlock/once. `mutex` vardır (lock/try_lock/unlock; önce atomic CAS, başarısızsa OS bekleme).
  `lock_guard`/RAII bekçi **yoktur**: kilit açık `unlock` ile bırakılır.
- [ ] **7.5** `std/sched`: **Go benzeri scheduler** (M:N): goroutine benzeri hafif görevler, kullanıcı
  alanı stack'leri ve bağlam değiştirme (6.3), her CPU için bir worker thread, iş çalma (work stealing),
  `spawn(fn, arg)`, `wait_all()`, `cpu()`, bloklayıcı I/O için OS olay mekanizması entegrasyonu
  (epoll/kqueue/IOCP benzeri; 7.1 syscall'larıyla). Compiler scheduler'ı bilmez.
- [ ] **7.6** xmake `std` hedefi: stdlib'i derleyip compiler modül arama yoluna kurar.
- [ ] **7.7** Compiler'dan kaldır: `spawn`/`go`, `print` özel işleme, `malloc/free/printf` prelude,
  `needs_heap_`/`needs_strings_`; `new` dilde kalır (4.11, kayıtlı ya da açık allocator), `delete` yoktur.
  Eski example/lib'ler yeni dile göre yeniden yazılır (bu aşamada).

**Kabul:** compiler kaynağında `spawn`, `thread`, `pthread`, `printf`, `malloc` geçmez (link bayrakları
hariç); stdlib hiçbir dış kütüphane çağırmaz (yalnızca `syscall`); Linux ve macOS'ta
(ve mümkünse Windows'ta) aynı kullanıcı kodu farklı `sys/*` ile çalışır; `--no-std` ile stdlib'siz
bir program derlenir.
