/* xl_sync — Windows stubs (fill in for real Win32 sync later). */

#include <stdint.h>
#include <stdlib.h>
#include <windows.h>

int64_t xl_mutex_init(void) { return 0; }
int32_t xl_mutex_lock(int64_t h) { (void)h; return -1; }
int32_t xl_mutex_trylock(int64_t h) { (void)h; return -1; }
int32_t xl_mutex_unlock(int64_t h) { (void)h; return -1; }
int64_t xl_cond_init(void) { return 0; }
int32_t xl_cond_wait(int64_t c, int64_t m) { (void)c; (void)m; return -1; }
int32_t xl_cond_signal(int64_t c) { (void)c; return -1; }
int32_t xl_cond_broadcast(int64_t c) { (void)c; return -1; }
int32_t xl_sleep_ms(int32_t ms) { if (ms > 0) Sleep((DWORD)ms); return 0; }
int32_t xl_cpu_count(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int32_t)info.dwNumberOfProcessors : 1;
}
int64_t xl_atomic_alloc(void) {
    LONG64* cell = (LONG64*)malloc(sizeof(LONG64));
    if (!cell) return 0;
    *cell = 0;
    return (int64_t)(intptr_t)cell;
}
int64_t xl_atomic_load(int64_t ptr) {
    if (!ptr) return 0;
    return (int64_t)InterlockedCompareExchange64((LONG64*)(intptr_t)ptr, 0, 0);
}
int32_t xl_atomic_store(int64_t ptr, int64_t val) {
    if (!ptr) return -1;
    InterlockedExchange64((LONG64*)(intptr_t)ptr, (LONG64)val);
    return 0;
}
int64_t xl_atomic_fetch_add(int64_t ptr, int64_t delta) {
    if (!ptr) return 0;
    return (int64_t)InterlockedExchangeAdd64((LONG64*)(intptr_t)ptr, (LONG64)delta);
}
int32_t xl_atomic_compare_exchange(int64_t ptr, int64_t expected, int64_t desired) {
    if (!ptr) return 0;
    LONG64 exp = (LONG64)expected;
    LONG64 prev = InterlockedCompareExchange64((LONG64*)(intptr_t)ptr, (LONG64)desired, exp);
    return prev == exp ? 1 : 0;
}
