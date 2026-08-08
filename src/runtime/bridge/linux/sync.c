/* xl_sync — mutex / cond / sleep / cpu_count / atomics for scheduler + sync frontend. */

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

int64_t xl_mutex_init(void) {
    pthread_mutex_t* m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (m == NULL) {
        return 0;
    }
    if (pthread_mutex_init(m, NULL) != 0) {
        free(m);
        return 0;
    }
    return (int64_t)(intptr_t)m;
}

int32_t xl_mutex_lock(int64_t handle) {
    if (handle == 0) {
        return -1;
    }
    return (int32_t)pthread_mutex_lock((pthread_mutex_t*)(intptr_t)handle);
}

int32_t xl_mutex_trylock(int64_t handle) {
    if (handle == 0) {
        return -1;
    }
    return (int32_t)pthread_mutex_trylock((pthread_mutex_t*)(intptr_t)handle);
}

int32_t xl_mutex_unlock(int64_t handle) {
    if (handle == 0) {
        return -1;
    }
    return (int32_t)pthread_mutex_unlock((pthread_mutex_t*)(intptr_t)handle);
}

int64_t xl_cond_init(void) {
    pthread_cond_t* c = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
    if (c == NULL) {
        return 0;
    }
    if (pthread_cond_init(c, NULL) != 0) {
        free(c);
        return 0;
    }
    return (int64_t)(intptr_t)c;
}

int32_t xl_cond_wait(int64_t cond, int64_t mutex) {
    if (cond == 0 || mutex == 0) {
        return -1;
    }
    return (int32_t)pthread_cond_wait((pthread_cond_t*)(intptr_t)cond,
                                      (pthread_mutex_t*)(intptr_t)mutex);
}

int32_t xl_cond_signal(int64_t cond) {
    if (cond == 0) {
        return -1;
    }
    return (int32_t)pthread_cond_signal((pthread_cond_t*)(intptr_t)cond);
}

int32_t xl_cond_broadcast(int64_t cond) {
    if (cond == 0) {
        return -1;
    }
    return (int32_t)pthread_cond_broadcast((pthread_cond_t*)(intptr_t)cond);
}

int32_t xl_sleep_ms(int32_t ms) {
    if (ms <= 0) {
        return 0;
    }
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0) {
    }
    return 0;
}

int32_t xl_cpu_count(void) {
#if defined(__APPLE__)
    int count = 1;
    size_t len = sizeof(count);
    if (sysctlbyname("hw.ncpu", &count, &len, NULL, 0) != 0 || count < 1) {
        return 1;
    }
    return (int32_t)count;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) {
        return 1;
    }
    return (int32_t)n;
#endif
}

int64_t xl_atomic_alloc(void) {
    _Atomic int64_t* cell = (_Atomic int64_t*)malloc(sizeof(_Atomic int64_t));
    if (cell == NULL) {
        return 0;
    }
    atomic_init(cell, 0);
    return (int64_t)(intptr_t)cell;
}

int64_t xl_atomic_load(int64_t ptr) {
    if (ptr == 0) {
        return 0;
    }
    return atomic_load((_Atomic int64_t*)(intptr_t)ptr);
}

int32_t xl_atomic_store(int64_t ptr, int64_t val) {
    if (ptr == 0) {
        return -1;
    }
    atomic_store((_Atomic int64_t*)(intptr_t)ptr, val);
    return 0;
}

int64_t xl_atomic_fetch_add(int64_t ptr, int64_t delta) {
    if (ptr == 0) {
        return 0;
    }
    return atomic_fetch_add((_Atomic int64_t*)(intptr_t)ptr, delta);
}

int32_t xl_atomic_compare_exchange(int64_t ptr, int64_t expected, int64_t desired) {
    if (ptr == 0) {
        return 0;
    }
    int64_t exp = expected;
    return atomic_compare_exchange_strong((_Atomic int64_t*)(intptr_t)ptr, &exp, desired) ? 1 : 0;
}
