/* time — Windows stub ABI (unsupported). */
#include <stdint.h>

int64_t xl_now_ms(void) { return 0; }
const char* xl_time_format(int64_t unix_ms) { (void)unix_ms; return ""; }
