#include <execinfo.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

static __thread jmp_buf xlang_panic_jmp;
static __thread int xlang_panic_active;
static __thread char xlang_panic_message[4096];
static __thread int xlang_panic_pending;

static void xlang_print_stack_trace(void) {
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

void xlang_panic(const char* message) {
    if (message == NULL) {
        message = "panic";
    }
    strncpy(xlang_panic_message, message, sizeof(xlang_panic_message) - 1);
    xlang_panic_message[sizeof(xlang_panic_message) - 1] = '\0';

    fprintf(stderr, "panic: %s\n", xlang_panic_message);
    xlang_print_stack_trace();

    if (xlang_panic_active) {
        xlang_panic_pending = 1;
        longjmp(xlang_panic_jmp, 1);
    }

    fprintf(stderr, "fatal panic (uncaught)\n");
    fflush(stderr);
    _exit(2);
}

int xlang_try_enter(void) {
    xlang_panic_pending = 0;
    xlang_panic_message[0] = '\0';
    xlang_panic_active = 1;
    return setjmp(xlang_panic_jmp);
}

void xlang_try_leave(void) {
    xlang_panic_active = 0;
}

const char* xlang_recover_message(void) {
    if (xlang_panic_pending) {
        xlang_panic_pending = 0;
        return xlang_panic_message;
    }
    return "";
}

int xlang_try_invoke0(int64_t entry) {
    if (entry == 0) {
        return 1;
    }
    int (*fn)(void) = (int (*)(void))(intptr_t)entry;
    if (xlang_try_enter() != 0) {
        xlang_try_leave();
        return 2;
    }
    const int rc = fn();
    xlang_try_leave();
    return rc;
}
