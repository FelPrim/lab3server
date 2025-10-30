#define _GNU_SOURCE

#include "network_setup.h"
#include "protocol.h"
#include "useful_stuff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

// Глобальные переменные
int epfd = 0;
SSL_CTX* ssl_ctx = NULL;
int tfd = 0;
int ufd = 0;

volatile sig_atomic_t stop_requested = 0;

void calling_stop(int signo) {
    stop_requested = 1;
}

inline static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;
    return 0;
}

inline static int socket_configure(int *sock_fd, struct addrinfo *result) {
    struct addrinfo *p;
    int oresult = 0;
    
    for (p = result; p != NULL; p = p->ai_next) {
        if ((*sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            perror("socket");
            continue;
        }
        
        int opt = 1;
        if (setsockopt(*sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            perror("setsockopt");
            continue;
        }
        
        if (bind(*sock_fd, p->ai_addr, p->ai_addrlen) < 0) {
            perror("bind");
            oresult = -3;
            continue;
        }
        break;
    }
    
    freeaddrinfo(result);
    
    if (set_nonblocking(*sock_fd)) {
        oresult = -4;
    }
    
    return oresult;
}

int simple_starting_actions() {
    srand((unsigned int)time(NULL));
    
    if (freopen("error.log", "w", stderr) == NULL) {
        perror("freopen failed");
        return EXIT_FAILURE;
    }
    
    if (setvbuf(stderr, NULL, _IOLBF, 0) != 0) {
        perror("setvbuf failed");
        return EXIT_FAILURE;
    }
    
    signal(SIGINT, calling_stop);
    signal(SIGTERM, calling_stop);
    return 0;
}

int init_ssl_ctx() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx) {
        fprintf(stderr, "SSL_CTX_new failed\n");
        ERR_print_errors_fp(stderr);
        return -1;
    }

    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);

    if (SSL_CTX_use_certificate_file(ssl_ctx, "/etc/letsencrypt/live/marrs73.ru/fullchain.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return -1;
    }
    
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, "/etc/letsencrypt/live/marrs73.ru/privkey.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return -1;
    }

    if (!SSL_CTX_check_private_key(ssl_ctx)) {
        fprintf(stderr, "Private key does not match the certificate\n");
        return -1;
    }

    if (!SSL_CTX_set_ciphersuites(ssl_ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256")) {
        fprintf(stderr, "Failed to set cipher suites\n");
        return -1;
    }

    SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
    SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_SERVER);

    fprintf(stderr, "SSL_CTX initialized successfully.\n");
    return 0;
}

void cleanup_ssl_ctx() {
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = NULL;
    }
    EVP_cleanup();
    ERR_free_strings();
    CRYPTO_cleanup_all_ex_data();
}

int tcp_socket_configuration() {
    struct addrinfo hints;
    struct addrinfo *result;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int status = getaddrinfo(NULL, TPSTR, &hints, &result);
    if (status) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }
    
    status = socket_configure(&tfd, result);
    if (status) {
        return EXIT_FAILURE;
    }
    
    if (listen(tfd, SOMAXCONN) < 0) {
        perror("listen");
        return EXIT_FAILURE;
    }
    
    return 0;
}

int udp_socket_configuration() {
    struct addrinfo hints;
    struct addrinfo *result;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    
    int status = getaddrinfo(NULL, UPSTR, &hints, &result);
    if (status) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }
    
    status = socket_configure(&ufd, result);
    if (status) {
        return EXIT_FAILURE;
    }
    
    return 0;
}

int epfd_configuration() {
    epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        return -1;
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data.fd = tfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &ev) == -1) {
        perror("epoll_ctl tfd");
        return -1;
    }
    
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = ufd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, ufd, &ev) == -1) {
        perror("epoll_ctl ufd");
        return -1;
    }
    
    return 0;
}

void MainState_construct() {
    ssl_ctx = NULL;
    epfd = 0;
    tfd = 0;
    ufd = 0;
}

void MainState_destruct() {
    if (epfd) {
        close(epfd);
        epfd = 0;
    }
    if (tfd) {
        close(tfd);
        tfd = 0;
    }
    if (ufd) {
        close(ufd);
        ufd = 0;
    }
    
    cleanup_ssl_ctx();
}

int handle_listen() {
    // Реализация принятия новых соединений
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int client_fd = accept(tfd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("accept");
        }
        return -1;
    }
    
    // Установка non-blocking режима
    if (set_nonblocking(client_fd) < 0) {
        close(client_fd);
        return -1;
    }
    
    // Создание SSL соединения
    SSL* ssl = SSL_new(ssl_ctx);
    if (!ssl) {
        close(client_fd);
        return -1;
    }
    
    SSL_set_fd(ssl, client_fd);
    
    // Создание структуры Connection
    Connection* conn = malloc(sizeof(Connection));
    if (!conn) {
        SSL_free(ssl);
        close(client_fd);
        return -1;
    }
    
    Connection_construct(conn);
    conn->client_fd = client_fd;
    conn->ssl = ssl;
    
    // Добавление в epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data.fd = client_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
        Connection_destruct(conn);
        free(conn);
        return -1;
    }
    
    Connection_add(conn);
    
    fprintf(stderr, "New client connected: %d\n", client_fd);
    return 0;
}

int handle_udp_packet() {
    // Базовая реализация обработки UDP пакетов
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[1024];
    
    ssize_t n = recvfrom(ufd, buffer, sizeof(buffer), 0, 
                        (struct sockaddr*)&client_addr, &client_len);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recvfrom");
        }
        return -1;
    }
    
    fprintf(stderr, "Received UDP packet, size: %zd\n", n);
    // Здесь должна быть логика обработки UDP пакетов
    
    return 0;
}
