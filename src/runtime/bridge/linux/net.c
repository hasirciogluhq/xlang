#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int is_any_host(const char* host) {
    if (host == NULL || host[0] == '\0') {
        return 1;
    }
    return strcmp(host, "0.0.0.0") == 0 || strcmp(host, "*") == 0;
}

int64_t xl_net_tcp_connect(const char* host, int32_t port) {
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", (int)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, portbuf, &hints, &res) != 0) {
        return -1;
    }

    int sock = -1;
    for (struct addrinfo* it = res; it != NULL; it = it->ai_next) {
        int candidate = (int)socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (candidate < 0) {
            continue;
        }
        if (connect(candidate, it->ai_addr, (socklen_t)it->ai_addrlen) == 0) {
            sock = candidate;
            break;
        }
        close(candidate);
    }

    freeaddrinfo(res);
    return (int64_t)sock;
}

int64_t xl_net_tcp_listen(const char* host, int32_t port) {
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", (int)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    const char* node = host;
    if (is_any_host(host)) {
        node = NULL;
    } else {
        hints.ai_flags = 0;
    }

    struct addrinfo* res = NULL;
    if (getaddrinfo(node, portbuf, &hints, &res) != 0) {
        return -1;
    }

    int listen_fd = -1;
    for (struct addrinfo* it = res; it != NULL; it = it->ai_next) {
        int sock = (int)socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock < 0) {
            continue;
        }

        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));

        if (bind(sock, it->ai_addr, (socklen_t)it->ai_addrlen) != 0) {
            close(sock);
            continue;
        }

        if (listen(sock, 128) != 0) {
            close(sock);
            continue;
        }

        listen_fd = sock;
        break;
    }

    freeaddrinfo(res);
    if (listen_fd < 0) {
        return -1;
    }
    return (int64_t)listen_fd;
}

int64_t xl_net_tcp_accept(int64_t listen_fd) {
    if (listen_fd < 0) {
        return -1;
    }
    const int client = (int)accept((int)listen_fd, NULL, NULL);
    if (client < 0) {
        return -1;
    }
    return (int64_t)client;
}

int32_t xl_net_send(int64_t fd, const char* data) {
    if (fd < 0 || data == NULL) {
        return -1;
    }
    const size_t len = strlen(data);
    const ssize_t n = send((int)fd, data, len, 0);
    return (int32_t)n;
}

const char* xl_net_recv(int64_t fd, int32_t max) {
    static __thread char buf[1024 * 1024];
    if (fd < 0 || max <= 0) {
        buf[0] = '\0';
        return buf;
    }
    size_t cap = (size_t)max;
    if (cap > sizeof(buf) - 1) {
        cap = sizeof(buf) - 1;
    }
    const ssize_t n = recv((int)fd, buf, cap, 0);
    if (n <= 0) {
        buf[0] = '\0';
        return buf;
    }
    buf[n] = '\0';
    return buf;
}

int32_t xl_net_close(int64_t fd) {
    if (fd < 0) {
        return -1;
    }
    return (int32_t)close((int)fd);
}
