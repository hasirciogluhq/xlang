/* thread — Windows stub ABI (unsupported). */
#include <stdint.h>

int64_t xl_thread_start(int64_t entry, int32_t arg) {
    (void)entry; (void)arg; return -1;
}
int32_t xl_thread_join(int64_t handle) { (void)handle; return -1; }
int32_t xl_thread_detach(int64_t handle) { (void)handle; return -1; }
