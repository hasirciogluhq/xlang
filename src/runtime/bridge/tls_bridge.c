#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

typedef struct {
    SSL* ssl;
    SSL_CTX* ctx;
    int fd;
} XlangTlsSession;

static int xlang_tcp_connect(const char* host, int port) {
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, portbuf, &hints, &res) != 0) {
        return -1;
    }

    int sock = -1;
    for (struct addrinfo* node = res; node != NULL; node = node->ai_next) {
        sock = (int)socket(node->ai_family, node->ai_socktype, node->ai_protocol);
        if (sock < 0) {
            continue;
        }
        if (connect(sock, node->ai_addr, (socklen_t)node->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);
    return sock;
}

static void xlang_ssl_init_once(void) {
    static int inited = 0;
    if (inited) {
        return;
    }
    OPENSSL_init_ssl(0, NULL);
    inited = 1;
}

int64_t xlang_tls_connect(const char* host, int32_t port) {
    xlang_ssl_init_once();

    int fd = xlang_tcp_connect(host, (int)port);
    if (fd < 0) {
        return -1;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == NULL) {
        close(fd);
        return -1;
    }

    SSL* ssl = SSL_new(ctx);
    if (ssl == NULL) {
        SSL_CTX_free(ctx);
        close(fd);
        return -1;
    }

    SSL_set_fd(ssl, fd);
#if defined(SSL_CTRL_SET_TLSEXT_HOSTNAME)
    SSL_set_tlsext_host_name(ssl, host);
#endif

    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return -1;
    }

    XlangTlsSession* session = (XlangTlsSession*)malloc(sizeof(XlangTlsSession));
    if (session == NULL) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return -1;
    }

    session->ssl = ssl;
    session->ctx = ctx;
    session->fd = fd;
    return (int64_t)(uintptr_t)session;
}

int32_t xlang_tls_send(int64_t handle, const char* data) {
    XlangTlsSession* session = (XlangTlsSession*)(uintptr_t)handle;
    if (session == NULL || data == NULL) {
        return -1;
    }
    int n = SSL_write(session->ssl, data, (int)strlen(data));
    return (int32_t)n;
}

char* xlang_tls_recv(int64_t handle, int32_t max) {
    XlangTlsSession* session = (XlangTlsSession*)(uintptr_t)handle;
    if (session == NULL || max <= 0) {
        char* empty = (char*)malloc(1);
        if (empty) {
            empty[0] = '\0';
        }
        return empty;
    }

    char* buf = (char*)malloc((size_t)max);
    if (buf == NULL) {
        char* empty = (char*)malloc(1);
        if (empty) {
            empty[0] = '\0';
        }
        return empty;
    }

    int n = SSL_read(session->ssl, buf, max);
    if (n <= 0) {
        free(buf);
        char* empty = (char*)malloc(1);
        if (empty) {
            empty[0] = '\0';
        }
        return empty;
    }

    char* out = (char*)realloc(buf, (size_t)n + 1);
    if (out == NULL) {
        out = buf;
    }
    out[n] = '\0';
    return out;
}

int32_t xlang_tls_close(int64_t handle) {
    XlangTlsSession* session = (XlangTlsSession*)(uintptr_t)handle;
    if (session == NULL) {
        return -1;
    }
    SSL_shutdown(session->ssl);
    SSL_free(session->ssl);
    SSL_CTX_free(session->ctx);
    close(session->fd);
    free(session);
    return 0;
}
