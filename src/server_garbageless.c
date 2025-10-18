#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <time.h>

#include <errno.h>
#include <unistd.h>
// чтобы VSCode не так сильно ругался
#ifdef __linux__
    #include <netdb.h>
#endif
#include <sys/types.h>
#ifdef __linux__
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif
#include <fcntl.h>
#ifdef __linux__
    #include <sys/epoll.h>
    #include <sys/eventfd.h>
#endif


#include <signal.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include "uthash.h"

#include "protocol.h"

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

//////////////////////////////////////////////////
// SIGNALS

volatile sig_atomic_t stop_requested = 0;

void calling_stop(int signo){
    stop_requested = 1;
}


#define MAX_EVENTS 64


//////////////////////////////////////////////////
// INTERNET
#define TPORT 23230
#define UPORT 23231
#define TPSTR "23230"
#define UPSTR "23231"

inline static int socket_configure(int *sock_fd, struct addrinfo *result){
    struct addrinfo *p;
    int oresult = 0;
    for (p = result; p != NULL; p=p->ai_next){
        if ((*sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0){
            perror("socket");
            continue;
        }
		int opt = 1;
        if (setsockopt(*sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
            perror("setsockopt");
            continue;
        }
        if (bind(*sock_fd, p->ai_addr, p->ai_addrlen) < 0){
            perror("bind");
            oresult = -3;
            continue;
        }
		break;
    }
	
	freeaddrinfo(result);		
	int flags = fcntl(*sock_fd, F_GETFL, 0);
	if (flags < 0){
		perror("fcntl_getfl");
		oresult = -4;
		goto sc_end;
	}
	if (fcntl(*sock_fd, F_SETFL, flags | O_NONBLOCK) < 0){
		perror("fcntl_setfl");
		oresult = -5;
		goto sc_end;
	}
	sc_end:
	return oresult;
}

inline static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;
    return 0;
}


#define BASIC_SZ 8192

struct Buffer{
    char mem[BASIC_SZ];
    uint32_t seek;
    uint32_t expected_endseek;
    // bool reading_data = expected_endseek != 0
};

inline static int Buffer_construct(struct Buffer* self){
    self->seek = 0;
    self->expected_endseek = 0;
    return 0;
}

inline static int Buffer_destruct(struct Buffer* self){
    return 0;
}

inline static int Buffer_clean(struct Buffer* self){
    self->seek = 0;
    self->expected_endseek = 0;
    return 0;
}

// -1 -> Ошибка
// 0 -> ждёт ещё
// 1 -> Полон
// 2 -> Переполнен
inline static int Buffer_write(struct Buffer* self, char *data, uint32_t size) {
    assert(self->expected_endseek < BASIC_SZ);
    if (self->seek + size > self->expected_endseek)
        return 2; // overflow
    memcpy(self->mem + self->seek, data, size);
    self->seek += size;
    return (self->seek == self->expected_endseek) ? 1 : 0;
}

static inline int Buffer_save(struct Buffer *b, const void *data, uint32_t len) {
    assert(len < BASIC_SZ);
    memcpy(b->mem, data, len);
    b->seek = 0; /* сколько уже отправлено */
    b->expected_endseek = len;
    return 0;
}

// outbuf
inline static int Buffer_read_to_SSL(struct Buffer* self, SSL *ssl) {
    assert(self->expected_endseek < BASIC_SZ);
    while (self->seek < self->expected_endseek) {
        int to_send = (int)(self->expected_endseek - self->seek);
        int written = SSL_write(ssl, self->mem + self->seek, to_send);
        if (written > 0) {
            self->seek += (uint32_t)written;
            continue;
        }
        int err = SSL_get_error(ssl, written);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ)
            return 0; // нужно подождать EPOLLOUT
        if (err == SSL_ERROR_ZERO_RETURN)
            return 3; // закрыто
        ERR_error_string_n(ERR_get_error(), self->mem, sizeof(self->mem));
        fprintf(stderr, "Buffer_read SSL error: %s\n", self->mem);
        return 3;
    }
    return 1; // всё отправлено
}

static int Buffer_read_from_ssl(struct Buffer *b, SSL *ssl) {
    assert(b->expected_endseek < BASIC_SZ);
    /* читаем в цикле до тех пор, пока не заполним ожидаемую длину или пока SSL не скажет WANT */
    while (b->seek < b->expected_endseek) {
        int toread = (int)(b->expected_endseek - b->seek);
        /* читаем по частям, чтобы не запрашивать слишком много */
        int r = SSL_read(ssl, b->mem + b->seek, toread);
        if (r > 0) {
            b->seek += (uint32_t)r;
            if (b->seek == b->expected_endseek) return 1; /* полностью прочитали */
            /* продолжаем читать в том же вызове, если есть данные */
            continue;
        }
        int err = SSL_get_error(ssl, r);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            /* частичное чтение/нет данных сейчас: дождёмся следующего EPOLLIN */
            return 0;
        } else if (err == SSL_ERROR_ZERO_RETURN) {
            /* orderly shutdown */
            return -1;
        } else {
            ERR_error_string_n(ERR_get_error(), b->mem, sizeof(b->mem));
            fprintf(stderr, "Buffer_read_from_ssl fatal SSL_read error: %s\n", b->mem);
            return -2;
        }
    }
    /* Если цикл не вошёл (seek >= expected_endseek) — считаем что уже готово */
    return 1;
}


typedef struct Connection{
    struct Buffer buffer;
    struct sockaddr_in udp_addr; 
    SSL *ssl;
    int client_fd; // key
    UT_hash_handle hh;
} Connection;

Connection *connections = NULL;

inline static int Connection_construct(Connection* self){
    self->ssl = NULL;
    self->client_fd = 0;
    return Buffer_construct(&self->buffer);
}

inline static int Connection_destruct(Connection* self){
    if (self->ssl)
        SSL_free(self->ssl);
    if (self->client_fd)
        close(self->client_fd);
    return Buffer_destruct(&self->buffer);
}

inline static void Connection_add(Connection* elem){
	HASH_ADD_INT(connections, client_fd, elem);
}

inline static Connection* Connection_find(int client_fd){
	Connection* result;
	HASH_FIND_INT(connections, &client_fd, result);
	return result;
}

inline static void Connection_delete(Connection* connection){
	HASH_DEL(connections, connection);
	Connection_destruct(connection);
	free(connection);
}

#define CALL_MAXSZ 64
#define CALL_NAME_SZ 7
#define SYMM_KEY_LEN 32

typedef struct Call{
    int participants[CALL_MAXSZ]; // первый участник - владелец конференции
    uint32_t count;
    char callname[CALL_NAME_SZ]; // key
    unsigned char symm_key[SYMM_KEY_LEN]; 
    uint8_t key_len;
    UT_hash_handle hh;
} Call;

Call *calls = NULL;


inline static int Call_construct(Call* self){
    memset(self->participants, 0, CALL_MAXSZ*sizeof(int));
    self->count = 0;
    static_assert(RAND_MAX == 2147483647);
    int r = rand();
    for (int i = 0; i < 6; ++i){
    	self->callname[i] = r%26+'A';
	r /= 26;
    }
    self->callname[6] = '\0';    
    self->callname[CALL_NAME_SZ-1] = '\0';
    self->key_len = 0;
    return 0;
}

inline static int Call_destruct(Call* self){
    if (self->key_len) {
        OPENSSL_cleanse(self->symm_key, self->key_len);
        self->key_len = 0;
    }
    return 0;
}

inline static void Call_add(Call* elem){
	HASH_ADD_STR(calls, callname, elem);
}

inline static Call* Call_find(char callname[7]){
	Call* result;
	HASH_FIND_STR(calls, callname, result);
	return result;
}

inline static void Call_delete(Call* call){
	HASH_DEL(calls, call);
	Call_destruct(call);
    free(call);
}

struct MainState{
	SSL_CTX* ssl_ctx;
    int epfd;
    int tfd;
    int ufd;
    struct epoll_event events[MAX_EVENTS];
};

inline static void MainState_construct(struct MainState* state){
    state->ssl_ctx = NULL;
    state->epfd = 0;
    state->tfd = 0;
    state->ufd = 0;

}

inline static void MainState_destruct(struct MainState* state){
    if (state->ssl_ctx) {
        SSL_CTX_free(state->ssl_ctx);
        state->ssl_ctx = NULL;
    }
    if (state->epfd)
        close(state->epfd);
    if (state->tfd)
        close(state->tfd);
    if (state->ufd)
        close(state->ufd);
}

inline static int simple_starting_actions(){
    srand((unsigned int)time(NULL));
    if (freopen("error.log", "w", stderr) == NULL){
        perror("freopen failed");
        return EXIT_FAILURE;
    }
    if (setvbuf(stderr, NULL, _IOLBF, 0) != 0) {
        perror("setvbuf failed");
        return EXIT_FAILURE;
    }
    
    signal(SIGINT, calling_stop);
    signal(SIGTERM, calling_stop);
}

inline static int init_ssl_ctx(struct MainState* state) {
    // 1. Инициализация OpenSSL (в современных версиях часто не требуется явно)
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // 2. Создаём контекст TLS-сервера (универсальный метод, поддерживает TLS 1.2/1.3)
    state->ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!state->ssl_ctx) {
        fprintf(stderr, "SSL_CTX_new failed\n");
        ERR_print_errors_fp(stderr);
        return -1;
    }

    // 3. Задаём минимально допустимую версию TLS (рекомендуется >= 1.2)
    SSL_CTX_set_min_proto_version(state->ssl_ctx, TLS1_2_VERSION);
    // (можно ограничить максимум TLS 1.3, если есть несовместимость)
    // SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);

    // 4. Загружаем сертификат и приватный ключ
    if (SSL_CTX_use_certificate_file(state->ssl_ctx, "/etc/letsencrypt/live/marrs73.ru/fullchain.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return -1;
    }
    if (SSL_CTX_use_PrivateKey_file(state->ssl_ctx, "/etc/letsencrypt/live/marrs73.ru/privkey.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return -1;
    }

    // Проверяем, что ключ соответствует сертификату
    if (!SSL_CTX_check_private_key(state->ssl_ctx)) {
        fprintf(stderr, "Private key does not match the certificate\n");
        return -1;
    }

    // 5. Настройки шифров (cipher suites)
    // — современный безопасный набор; можно адаптировать при необходимости
    if (!SSL_CTX_set_cipher_list(state->ssl_ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256")) {
        fprintf(stderr, "Failed to set cipher list\n");
        return -1;
    }

    // 6. (опционально) запретить старые протоколы / компрессию / слабые шифры
    SSL_CTX_set_options(state->ssl_ctx, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_options(state->ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);

    // 7. (опционально) можно включить сессионный кэш, если сервер долгоживущий
    SSL_CTX_set_session_cache_mode(state->ssl_ctx, SSL_SESS_CACHE_SERVER);

    fprintf(stderr, "SSL_CTX initialized successfully.\n");
    return 0;
}

inline static void cleanup_ssl_ctx() {
    EVP_cleanup();              
    ERR_free_strings();         
    CRYPTO_cleanup_all_ex_data();
}

inline static int tcp_socket_configuration(int* tfd){
    *tfd = 0;
    struct addrinfo hints;
    struct addrinfo *result;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int status = getaddrinfo(NULL, TPSTR, &hints, &result);
    if (status){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }
    status = socket_configure(tfd, result);
    if (status){
        return EXIT_FAILURE;
    }
    if (listen(*tfd, SOMAXCONN) < 0){
        perror("listen");
	    return EXIT_FAILURE;
    }
    return 0;
}

inline static int udp_socket_configuration(int* ufd){
    *ufd = 0;
    struct addrinfo hints;
    struct addrinfo *result;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    
    int status =  getaddrinfo(NULL, UPSTR, &hints, &result);
    if (status){
    	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }
    
    status = socket_configure(ufd, result);
    if (status){
        return EXIT_FAILURE;
    }
    return 0;
}

inline static int epfd_configuration(struct MainState* state){
    state->epfd = epoll_create1(0);
    if (state->epfd == -1){
    	perror("epoll_create1");
	    return -1;
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN; // событие - ввод. EPOLLOUT - вывод, EPOLLRDHUP - отсоединение
    ev.data.fd = state->tfd;
    epoll_ctl(state->epfd, EPOLL_CTL_ADD, state->tfd, &ev);
    
    ev.events = EPOLLIN;
    ev.data.fd = state->ufd;
    epoll_ctl(state->epfd, EPOLL_CTL_ADD, state->ufd, &ev);
    return 0;
}

int buffer_ssl_read(Connection *c, size_t len) {
    while (c->buffer.size < len) {
        int n = SSL_read(c->ssl,
                         c->buffer.mem + c->buffer.size,
                         BASIC_SZ - c->buffer.size);
        if (n <= 0) {
            int err = SSL_get_error(c->ssl, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                return 0; // пока недостаточно данных, ждём следующего epoll
            return -1; // ошибка или закрытие
        }
        c->buffer.size += n;
    }
    return 1; // получили хотя бы len байт
}


inline static void handle_callstart_server(struct MainState *state, Connection *c){

}

inline static void handle_callend_server(struct MainState *state, Connection *c){

}

inline static void handle_calljoin_server(struct MainState *state, Connection *c){

}

inline static void handle_callleave_server(struct MainState *state, Connection *c){

}

inline static void handle_errorunknown_server(struct MainState *state, Connection *c){

}

inline static void handle_udp_packet(struct MainState* state){

}

inline static void handle_listen(struct MainState* state){

    struct sockaddr_storage peer;
    socklen_t plen = sizeof(peer);
    int client_fd;

#ifdef SOCK_NONBLOCK
    client_fd = accept4(state->tfd, (struct sockaddr*)&peer, &plen, SOCK_NONBLOCK);
#else
    client_fd = accept(state->tfd, (struct sockaddr*)&peer, &plen);
    if (client_fd >= 0) {
        if (set_nonblocking(client_fd) == -1) {
            perror("set_nonblocking(accepted)");
            close(client_fd);
            return;
        }
    }
#endif

    if (client_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        perror("accept");
        return;
    }

    int one = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

    Connection *c = calloc(1, sizeof(Connection));
    if (!c) {
        perror("calloc Connection");
        close(client_fd);
        return;
    }

    Connection_construct(c);
    c->client_fd = client_fd;
    memset(&c->udp_addr, 0, sizeof(c->udp_addr));
    struct sockaddr_in *sa = (struct sockaddr_in*)&peer;
    c->udp_addr.sin_family = AF_INET;
    c->udp_addr.sin_addr = sa->sin_addr;
    c->udp_addr.sin_port = 0; 

    SSL *ssl = SSL_new(state->ssl_ctx);
    if (!ssl) {
        fprintf(stderr, "SSL_new failed\n");
        Connection_destruct(c);
        return;
    }
    c->ssl = ssl;
    SSL_set_fd(ssl, client_fd);
    SSL_set_accept_state(ssl); // серверный режим

    // Попробуем выполнить handshake немедленно (non-blocking). Возможно
    // потребуются дополнительные EPOLL события для завершения.
    int ret = SSL_accept(ssl);
    int ssl_err = SSL_get_error(ssl, ret);
    int handshake_done = 0;

    if (ret == 1) {
        handshake_done = 1;
    } else if (ret <= 0) {
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            handshake_done = 0;
        } else {
            ERR_error_string_n(ERR_get_error(), c->buffer.mem, sizeof(c->buffer.mem));
            fprintf(stderr, "SSL_accept error: %s\n", c->buffer.mem);
            Connection_destruct(c);
            return;
        }
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    if (!handshake_done) {
        ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
    } else {
        ev.events = EPOLLIN | EPOLLRDHUP;
    }
    ev.data.fd = client_fd;

    if (epoll_ctl(state->epfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
        perror("epoll_ctl ADD client_fd");
        Connection_destruct(c);
        return;
    }
    
    // Добавляем в хэш connections
    Connection_add(c); // ваша функция добавляет по client_fd

    // Логируем
    char addrbuf[INET_ADDRSTRLEN] = "<unknown>";
    if (peer.ss_family == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in*)&peer;
        inet_ntop(AF_INET, &sa->sin_addr, addrbuf, sizeof(addrbuf));
    }
    fprintf(stderr, "Accepted client %d from %s (handshake %s)\n",
            client_fd, addrbuf, handshake_done ? "done" : "pending");

    // Если handshake закончен сразу — вы, возможно, хотите тут же читать
    // первый защищённый пакет (например, чтобы клиент сразу прислал UDP-порт).
    // Я не делаю чтение здесь, оставляю это на handle_client при EPOLLIN:
    //   handle_client() должен проверять c->ssl и вызывать SSL_read,
    //   а при получении специального сообщения обновлять c->udp_addr.sin_port.
}

inline static void handle_unfinished_handshake(struct MainState* state, int fd, SSL *ssl, Connection *c){
    int ret = SSL_accept(ssl);
    int ssl_err = SSL_get_error(ssl, ret);
    if (ret == 1) {
        /* Handshake завершён — переключаем epoll на нормальный режим (только чтение) */
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = fd;
        if (epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
            perror("epoll_ctl MOD after handshake");
        }
        fprintf(stderr, "Handshake finished for fd=%d\n", fd);
    } else {
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            /* Надо подождать дополнительных событий — ничего не делаем */
            return;
        } else {
            /* Фатальная ошибка handshake */
            ERR_error_string_n(ERR_get_error(), c->buffer.mem, sizeof(c->buffer.mem));
            fprintf(stderr, "SSL_accept error (continuation) for fd=%d: %s\n", fd, c->buffer.mem);
            epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, NULL);
            Connection_delete(c);
        }
    }
}

inline static void handle_ordinary_connection(struct MainState* state, int fd, SSL *ssl, Connection *c){
    unsigned char opcode;
    int n = SSL_read(ssl, &opcode, 1);
    if (n <= 0) {
        int ssl_err = SSL_get_error(ssl, n);
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            /* нет данных сейчас — ждём следующего EPOLLIN */
            return;
        }

        /* Клиент закрыл TLS-соединение корректно */
        if (ssl_err == SSL_ERROR_ZERO_RETURN) {
            fprintf(stderr, "Client (fd=%d) closed TLS (close_notify)\n", fd);
        } else {
            ERR_error_string_n(ERR_get_error(), c->buffer.mem, sizeof(c->buffer.mem));
            fprintf(stderr, "SSL_read error on fd=%d: %s\n", fd, c->buffer.mem);
        }

        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, NULL);
        Connection_delete(c);
        return;
    }

    switch (opcode) {
        case CALLSTARTCLIENT:
            /* Клиент запрашивает создание конференции (payload = только 1 байт) */
            handle_callstart_server(state, c);
            break;
        case CALLENDCLIENT:
            /* После байта идёт имя конференции — обработчик должен прочитать оставшиеся данные (через SSL_read) */
            handle_callend_server(state, c);
            break;
        case CALLJOINCLIENT:
            handle_calljoin_server(state, c);
            break;
        case CALLLEAVECLIENT:
            handle_callleave_server(state, c);
            break;
        case ERRORUNKNOWNCLIENT:
            handle_errorunknown_server(state, c);
            break;
        default:
            /* Неизвестный/неподдерживаемый опкод — можно отправить один байт ошибки или просто игнорировать */
            {
                unsigned char errbyte = ERRORUNKNOWNSERVER; /* замените реальным кодом, если он есть */
                SSL_write(ssl, &errbyte, 1);
            }
            break;
    }
}

inline static void handle_client(struct MainState* state, int fd){
    Connection *c = Connection_find(fd);
    if (!c) {
        /* Неожиданно — нет такого connection: удалить из epoll и закрыть */
        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return;
    }

    SSL *ssl = c->ssl;
    if (!ssl) {
        /* Нет SSL — считаем это некорректным, закрываем */
        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, NULL);
        Connection_delete(c);
        return;
    }

    /* 1) Если handshake ещё не завершён — продолжить его (non-blocking) */
    if (!SSL_is_init_finished(ssl)) 
        handle_unfinished_handshake(state, fd, ssl, c);
    else
        handle_ordinary_connection(state, fd, ssl, c);
}


int main(){
    struct MainState state;
    MainState_construct(&state);
    int status = 0;
    if (status = simple_starting_actions())
        goto State_destruct;
    if (status = init_ssl_ctx(&state))
        goto State_destruct;
    if (status = tcp_socket_configuration(&state.tfd))
        goto SSL_cleanup;
    if (status = udp_socket_configuration(&state.ufd))
        goto SSL_cleanup;
    if (status = epfd_configuration(&state))
        goto SSL_cleanup;
    
    while (!stop_requested){
	    int n = epoll_wait(state.epfd, state.events, MAX_EVENTS, -1);
	    for (int i = 0; i < n; ++i){
	    	int fd = events[i].data.fd;
		    if (likely(fd == state.ufd))
                handle_udp_packet(&state);
		    else if (unlikely(fd == state.tfd))
                handle_listen(&state);
		    else 
                handle_client(&state, fd);
	    }
    }

SSL_cleanup:
    cleanup_ssl_ctx();
State_destruct:
    MainState_destruct(&state);
    return status;
}
