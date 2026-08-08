/* panic — Windows stub ABI (unsupported). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void xl_panic(const char* message) {
    fprintf(stderr, "panic: %s\n", message != NULL ? message : "panic");
    abort();
}
int xl_try_enter(void) { return 0; }
void xl_try_leave(void) {}
const char* xl_recover_message(void) { return ""; }
int xl_try_invoke0(int64_t entry) { (void)entry; return -1; }
