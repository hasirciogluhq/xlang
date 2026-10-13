## 15. Stdlib ile thread

Compiler thread/scheduler bilmez. Varsayılan stdlib (xl ile yazılmış) her OS'un çekirdek ABI'sini ham
`syscall` ile çağırarak thread oluşturur; OS farkı `#if os` ile stdlib içinde çözülür. Stdlib'in içinde
**Go benzeri bir scheduler** vardır (hafif görevler, her CPU için worker, iş çalma).

Önce stdlib'in kendi içinden bir parça (kullanıcı görmez):

```xlang
// std/sys/thread.xl
#if os == "linux"
    // clone(flags, stack, ptid, ctid, tls)
    fn os_thread_create(stack: *void, entry: fn(*void), arg: *void) -> i64 {
        return syscall(CLONE_NR, CLONE_FLAGS, stack as i64, 0, 0, 0)
    }
#elif os == "macos"
    // bsdthread_create(func, arg, stack, pthread, flags)
    fn os_thread_create(stack: *void, entry: fn(*void), arg: *void) -> i64 {
        return syscall(BSDTHREAD_CREATE_NR, entry as i64, arg as i64, stack as i64, 0, 0)
    }
#elif os == "windows"
    // NtCreateThreadEx(...); SSN, -D WIN_BUILD ile seçilen tablodan gelir
    fn os_thread_create(stack: *void, entry: fn(*void), arg: *void) -> i64 {
        return syscall(NT_CREATE_THREAD_EX_SSN, 0, 0, 0, 0)
    }
#endif
```

Kullanıcı kodu:

```xlang
import "std/sched" as sched
import "std/io" as io

struct Job { id: i32 }

fn worker(arg: *void) {
    local j: *Job = arg as *Job
    io.print("job {}\n", j.id)
}

fn main() -> i32 {
    local a: Job = Job { id: 1 }
    local b: Job = Job { id: 2 }
    sched.spawn(worker, &a as *void)
    sched.spawn(worker, &b as *void)
    sched.wait_all()
    return 0
}
```

```
kullanıcı kodu
   │   import "std/sched", "std/thread", "std/io"
   ▼
xlang stdlib            thread · sync · sched · io · mem
   │   #if os == ...
   ▼
std/sys/{linux,macos,windows}     yalnızca syscall(...), dış kütüphane yok
   ▼
compiler çekirdeği:  syscall · __builtin_atomic_* · fn pointer · inline asm
```

`go`/`spawn` anahtar sözcüğü yoktur; hepsi stdlib fonksiyonudur.

### Stdlib isteğe bağlıdır

Stdlib bir compiler bayrağıyla seçilir; hiçbir şey zorunlu kılınmaz.

| Bayrak | Anlam |
|--------|-------|
| (varsayılan) | Dile gömülü varsayılan stdlib |
| `--no-std` | Stdlib yok: yalnızca dil + global yerleşikler (`syscall`, atomic...) |
| `--std=<yol>` | Başka bir stdlib kök dizini |
| `--std-features=sched,gpu` | Stdlib özelliklerini aç/kapat (ör. `-default-allocator`: stdlib'in `register(allocator)` kaydını kapatır) |

```
xlang build app.xl                       # varsayılan stdlib
xlang build kernel.xl --no-std           # stdlib'siz
xlang build app.xl --std=./mystd         # kendi stdlib'in
```

### Scheduler (Go benzeri)

```
M:N model

   goroutine benzeri görevler (G)         worker thread'ler (M)         OS thread'leri
   +----+ +----+ +----+ +----+            +---------+  +---------+
   | G1 | | G2 | | G3 | | G4 |  ───────►  | worker0 |  | worker1 |  ───►  clone / bsdthread / NtCreateThreadEx
   +----+ +----+ +----+ +----+            +----+----+  +----+----+
        yerel kuyruk  ◄── iş çalma ──►  yerel kuyruk
```

| Parça | Açıklama |
|-------|----------|
| Görev | Kendi stack'i olan hafif birim; bağlam değişimi inline asm ile |
| Worker | CPU sayısı kadar OS thread'i |
| İş çalma | Boş worker, başkasının kuyruğundan görev alır |
| Bloklama | Bloklayıcı I/O, OS olay mekanizması (epoll/kqueue/IOCP benzeri) ile görev park edilir |
