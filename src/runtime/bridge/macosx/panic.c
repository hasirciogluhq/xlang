#include <execinfo.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

static __thread jmp_buf xl_panic_jmp;
static __thread int xl_panic_active;
static __thread char xl_panic_message[4096];
static __thread int xl_panic_pending;

static void xl_print_stack_trace(void) {
    void* frames[64];
    const int count = backtrace(frames, 64);
    char** symbols = backtrace_symbols(frames, count);
    if (symbols == NULL) {
        fprintf(stderr, "  (stack trace unavailable)\n");
        return;
    }
    for (int i = 0; i < count; ++i) {
        fprintf(stderr, "  %s\n", symbols[i]);
    }
    free(symbols);
}

void xl_panic(const char* message) {
    if (message == NULL) {
        message = "panic";
    }
    strncpy(xl_panic_message, message, sizeof(xl_panic_message) - 1);
    xl_panic_message[sizeof(xl_panic_message) - 1] = '\0';

    fprintf(stderr, "panic: %s\n", xl_panic_message);
    xl_print_stack_trace();

    if (xl_panic_active) {
        xl_panic_pending = 1;
        longjmp(xl_panic_jmp, 1);
    }

    fprintf(stderr, "fatal panic (uncaught)\n");
    fflush(stderr);
    _exit(2);
}

int xl_try_enter(void) {
    xl_panic_pending = 0;
    xl_panic_message[0] = '\0';
    xl_panic_active = 1;
    return setjmp(xl_panic_jmp);
}

void xl_try_leave(void) {
    xl_panic_active = 0;
}

const char* xl_recover_message(void) {
    if (xl_panic_pending) {
        xl_panic_pending = 0;
        return xl_panic_message;
    }
    return "";
}

int xl_try_invoke0(int64_t entry) {
    if (entry == 0) {
        return 1;
    }
    int (*fn)(void) = (int (*)(void))(intptr_t)entry;
    if (xl_try_enter() != 0) {
        xl_try_leave();
        return 2;
    }
    const int rc = fn();
    xl_try_leave();
    return rc;
}
