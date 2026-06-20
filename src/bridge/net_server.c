#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int is_any_host(const char* host) {
    if (host == NULL || host[0] == '\0') {
        return 1;
    }
    return strcmp(host, "0.0.0.0") == 0 || strcmp(host, "*") == 0;
}

int64_t xlang_net_tcp_listen(const char* host, int32_t port) {
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

int64_t xlang_net_tcp_accept(int64_t listen_fd) {
    if (listen_fd < 0) {
        return -1;
    }
    const int client = (int)accept((int)listen_fd, NULL, NULL);
    if (client < 0) {
        return -1;
    }
    return (int64_t)client;
}
