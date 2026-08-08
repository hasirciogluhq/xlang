/* xl_thread — pthread helpers for xlang scheduler / sync. */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    int32_t (*fn)(int32_t);
    int32_t arg;
} XlThreadCtx;

static void* xl_thread_bootstrap(void* arg) {
    XlThreadCtx* ctx = (XlThreadCtx*)arg;
    int32_t (*fn)(int32_t) = ctx->fn;
    int32_t a = ctx->arg;
    free(ctx);
    if (fn != NULL) {
        (void)fn(a);
    }
    return NULL;
}

int64_t xl_thread_start(int64_t entry, int32_t arg) {
    if (entry == 0) {
        return -1;
    }
    XlThreadCtx* ctx = (XlThreadCtx*)malloc(sizeof(XlThreadCtx));
    if (ctx == NULL) {
        return -1;
    }
    ctx->fn = (int32_t (*)(int32_t))(intptr_t)entry;
    ctx->arg = arg;
    pthread_t thr;
    if (pthread_create(&thr, NULL, xl_thread_bootstrap, ctx) != 0) {
        free(ctx);
        return -1;
    }
    return (int64_t)thr;
}

int32_t xl_thread_join(int64_t handle) {
    if (handle == 0 || handle == -1) {
        return -1;
    }
    return (int32_t)pthread_join((pthread_t)handle, NULL);
}

int32_t xl_thread_detach(int64_t handle) {
    if (handle == 0 || handle == -1) {
        return -1;
    }
    return (int32_t)pthread_detach((pthread_t)handle);
}
