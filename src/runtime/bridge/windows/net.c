/* net — Windows stub ABI (unsupported). */
#include <stdint.h>

int64_t xl_net_tcp_connect(const char* host, int32_t port) {
    (void)host; (void)port; return -1;
}
int64_t xl_net_tcp_listen(const char* host, int32_t port) {
    (void)host; (void)port; return -1;
}
int64_t xl_net_tcp_accept(int64_t listen_fd) {
    (void)listen_fd; return -1;
}
int32_t xl_net_send(int64_t fd, const char* data) {
    (void)fd; (void)data; return -1;
}
const char* xl_net_recv(int64_t fd, int32_t max) {
    (void)fd; (void)max; return "";
}
int32_t xl_net_close(int64_t fd) {
    (void)fd; return -1;
}
