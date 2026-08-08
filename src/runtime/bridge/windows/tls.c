/* tls — Windows stub ABI (unsupported). */
#include <stdint.h>

int64_t xl_tls_connect(const char* host, int32_t port) {
    (void)host; (void)port; return -1;
}
int32_t xl_tls_send(int64_t handle, const char* data) {
    (void)handle; (void)data; return -1;
}
char* xl_tls_recv(int64_t handle, int32_t max) {
    (void)handle; (void)max; return (char*)"";
}
int32_t xl_tls_close(int64_t handle) {
    (void)handle; return -1;
}
