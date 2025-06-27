#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <time.h>

#include <errno.h>
#include <unistd.h>

#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>



#include <signal.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include "uthash.h"

int epfd;
SSL_CTX* ssl_ctx;
int tfd;
int ufd;
#define EPFD_IS_DEFINED

#include "protocol.h"
#include "useful_stuff.h"
#include "nonblocking_ssl.h"
//////////////////////////////////////////////////
// SIGNALS

volatile sig_atomic_t stop_requested = 0;

void calling_stop(int signo){
    stop_requested = 1;
}


#define MAX_EVENTS 64


//////////////////////////////////////////////////
// INTERNET
inline static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;
    return 0;
}


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
    if (set_nonblocking(*sock_fd))
        oresult = -4;
	sc_end:
	return oresult;
}



typedef struct Call{
    int participants[CALL_MAXSZ]; // первый участник - владелец конференции
    uint32_t count;
    char callname[CALL_NAME_SZ]; // key
    unsigned char symm_key[SYMM_KEY_LEN]; 
    UT_hash_handle hh;
} Call;

Call *calls = NULL;

#define DOING_NOTHING 0
#define SSL_ACCEPTING -1

typedef struct Connection{
    struct NBSSL_Buffer ssl_in;
    struct NBSSL_Buffer ssl_out;
    struct sockaddr_in udp_addr; 
    SSL *ssl;
    int client_fd; // key
    int flags;
    int status;
    Call* *calls;
    int calls_size;
    int calls_count;
    UT_hash_handle hh;
} Connection;

Connection *connections = NULL;

inline static void Connection_construct(Connection* self){
    self->ssl = NULL;
    self->client_fd = 0;
    sslbuf_clean(&self->ssl_in);
    sslbuf_clean(&self->ssl_out);
    self->flags = UNINITIALIZED;
    self->status = DOING_NOTHING;
    self->calls_size = 8;
    self->calls = calloc(8, sizeof(Call*));
    self->calls_count = 0;
}

inline static void Connection_destruct(Connection* self){
    if (self->ssl)
        SSL_free(self->ssl);
    if (self->client_fd)
        close(self->client_fd);
    free(self->calls);
    self->calls_size=0;
    self->calls_count=0;
    free(self);
    return;
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
    if (connection->client_fd)
        epoll_ctl(epfd, EPOLL_CTL_DEL, connection->client_fd, NULL);
	Connection_destruct(connection);
}

inline static void Connection_delete_with_disconnectfromcalls(Connection* self);

inline static int Call_construct(Call* self){
    memset(self->participants, 0, CALL_MAXSZ*sizeof(int));
    self->count = 0;
    static_assert(RAND_MAX == 2147483647, "26^6");
    int r = rand();
    for (int i = 0; i < CALL_NAME_SZ-1; ++i){
    	self->callname[i] = r%26+'A';
	    r /= 26;
    }
    self->callname[CALL_NAME_SZ-1] = '\0';
    RAND_priv_bytes(self->symm_key, SYMM_KEY_LEN);
    return 0;
}

inline static int Call_destruct(Call* self){
    OPENSSL_cleanse(self->symm_key, SYMM_KEY_LEN);
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

inline static void MainState_construct(){
    ssl_ctx = NULL;
    epfd = 0;
    tfd = 0;
    ufd = 0;
}

inline static void MainState_destruct(){
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = NULL;
    }
    if (epfd)
        close(epfd);
    if (tfd)
        close(tfd);
    if (ufd)
        close(ufd);
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
    return 0;
}

inline static int init_ssl_ctx() {
    // 1. Инициализация OpenSSL (в современных версиях часто не требуется явно)
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // 2. Создаём контекст TLS-сервера (универсальный метод, поддерживает TLS 1.2/1.3)
    ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx) {
        fprintf(stderr, "SSL_CTX_new failed\n");
        ERR_print_errors_fp(stderr);
        return -1;
    }

    // 3. Задаём минимально допустимую версию TLS (рекомендуется >= 1.2)
    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
    // (можно ограничить максимум TLS 1.3, если есть несовместимость)
    // SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);

    // 4. Загружаем сертификат и приватный ключ
    if (SSL_CTX_use_certificate_file(ssl_ctx, "/etc/letsencrypt/live/marrs73.ru/fullchain.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return -1;
    }
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, "/etc/letsencrypt/live/marrs73.ru/privkey.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return -1;
    }

    // Проверяем, что ключ соответствует сертификату
    if (!SSL_CTX_check_private_key(ssl_ctx)) {
        fprintf(stderr, "Private key does not match the certificate\n");
        return -1;
    }

    // 5. Настройки шифров (cipher suites)
    // — современный безопасный набор; можно адаптировать при необходимости
    if (!SSL_CTX_set_ciphersuites(ssl_ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256")) {
        fprintf(stderr, "Failed to set cipher suites\n");
        return -1;
    }

    // 6. (опционально) запретить старые протоколы / компрессию / слабые шифры
    SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);

    // 7. (опционально) можно включить сессионный кэш, если сервер долгоживущий
    SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_SERVER);

    fprintf(stderr, "SSL_CTX initialized successfully.\n");
    return 0;
}

inline static void cleanup_ssl_ctx() {
    EVP_cleanup();              
    ERR_free_strings();         
    CRYPTO_cleanup_all_ex_data();
}

inline static int tcp_socket_configuration(){
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
    status = socket_configure(&tfd, result);
    if (status){
        return EXIT_FAILURE;
    }
    if (listen(tfd, SOMAXCONN) < 0){
        perror("listen");
	    return EXIT_FAILURE;
    }
    return 0;
}

inline static int udp_socket_configuration(){
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
    
    status = socket_configure(&ufd, result);
    if (status){
        return EXIT_FAILURE;
    }
    return 0;
}

inline static int epfd_configuration(){
    epfd = epoll_create1(0);
    if (epfd == -1){
    	perror("epoll_create1");
	    return -1;
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP; // событие - ввод. EPOLLOUT - вывод, EPOLLRDHUP - отсоединение
    ev.data.fd = tfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &ev);
    
    ev.events = EPOLLIN;
    ev.data.fd = ufd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ufd, &ev);
    return 0;
}

inline static int handle_info_client(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    if (c->status != INFOCLIENT){
        sslbuf_clean(buf);
        c->status = INFOCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, &c->ssl_in);
    switch(ret){
        case CAUGHT_ERROR:{
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htonl((uint32_t) buf->seekend)};
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
            
        }
        case IS_COMPLETED:{
            c->status = DOING_NOTHING;
            break;
        }
        case ISNT_COMPLETED:{
            break;
        }
    }
    return ret;
}

/*
inline static int handle_errorcalljoinmember(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    if (c->status != INFOCLIENT){
        sslbuf_clean(buf);
        c->status = INFOCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, &c->ssl_in);
}
inline static int handle_errorcallnotmember(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;

}
inline static int handle_errorcallnopermissions(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;


}
inline static int handle_errorcallnotowner(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
}

inline static int handle_errorcallnotexists(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
}
*/

inline static int handle_callstart_server(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    
    struct NBSSL_Buffer* buf = &c->ssl_out;
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret){
        case CAUGHT_ERROR:
        {
            Connection_delete_with_disconnectfromcalls(c);
            break;
        }
        case IS_COMPLETED:
        {
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        }
        case ISNT_COMPLETED:
        {
            c->status = CALLSTARTSERVER;
        }
    }
    return ret;
}

inline static int handle_callend_server(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;

    struct NBSSL_Buffer* buf = &c->ssl_out;
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret){
        case CAUGHT_ERROR:
        {
            Connection_delete_with_disconnectfromcalls(c);
            break;
        }
        case IS_COMPLETED:
        {
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        }
        case ISNT_COMPLETED:
        {
            c->status = CALLENDSERVER;
        }
    }
    return ret;
}

inline static int handle_calljoin_server(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;

    struct NBSSL_Buffer* buf = &c->ssl_out;
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret){
        case CAUGHT_ERROR:
        {
            Connection_delete_with_disconnectfromcalls(c);
            break;
        }
        case IS_COMPLETED:
        {
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        }
        case ISNT_COMPLETED:
        {
            c->status = CALLJOINSERVER;
        }
    }
    return ret;
}

inline static int handle_callleave_server(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_out;
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret){
        case CAUGHT_ERROR:
        {
            Connection_delete_with_disconnectfromcalls(c);
            break;
        }
        case IS_COMPLETED:
        {
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        }
        case ISNT_COMPLETED:
        {
            c->status = CALLLEAVESERVER;
        }
    }
    return ret;
}

inline static int handle_callleaveowner_server(Connection *c){
    SSL *ssl = c->ssl;
    int fd = c->client_fd;

    struct NBSSL_Buffer* buf = &c->ssl_out;
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret){
        case CAUGHT_ERROR:
        {
            Connection_delete_with_disconnectfromcalls(c);
            break;
        }
        case IS_COMPLETED:
        {
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        }
        case ISNT_COMPLETED:
        {
            c->status = CALLLEAVEOWNERSERVER;
        }
    }
    return ret;
}

inline static int handle_errorunknown_server(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;

    struct NBSSL_Buffer* buf = &c->ssl_out;
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret){
        case CAUGHT_ERROR:
        {
            Connection_delete_with_disconnectfromcalls(c);
            break;
        }
        case IS_COMPLETED:
        {
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        }
        case ISNT_COMPLETED:
        {
            c->status = ERRORUNKNOWNSERVER;
        }
    }
    return ret;
}


inline static int handle_callstart_client(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    /*if (c->status != CALLSTARTCLIENT){
        sslbuf_clean(buf);
        c->status = CALLSTARTCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, &c->ssl_in);
    switch(ret){
        case CAUGHT_ERROR:{
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htons(buf->seekend)};
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
            
        }
        case IS_COMPLETED:{
            struct ClientCommand callstart = {.};
            c->status = DOING_NOTHING;
            break;
        }
        case ISNT_COMPLETED:{
            break;
        }
    }
        
    return ret;
    вырожденный случай пустого сообщения    
    */
    Call* call = calloc(1, sizeof(Call));
    Call_construct(call);
    struct CallStartInfo info = {
        .command = CALLSTARTSERVER,
        .creator_fd = htonl((uint32_t) c->client_fd)
    };
    memcpy(info.callname, call->callname, CALL_NAME_SZ);
    memcpy(info.sym_key, call->symm_key, SYMM_KEY_LEN);
    sslbuf_clean(&c->ssl_out);
    sslbuf_write(&c->ssl_out, &info, sizeof(info));
    c->calls[c->calls_count] = call;
    c->calls_count++;
    Call_add(call);
    return handle_callstart_server(c);

}

inline static int handle_callend_client(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    if (c->status != CALLENDCLIENT){
        sslbuf_clean(buf);
        c->status = CALLENDCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, &c->ssl_in);
    switch(ret){
        case CAUGHT_ERROR:{
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htonl((uint32_t) buf->seekend)};
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
            
        }
        case IS_COMPLETED:{
            c->status = DOING_NOTHING;
            char callname[CALL_NAME_SZ];
            sslbuf_read(buf, callname, CALL_NAME_SZ);
            Call* call = Call_find(callname);
            char call_found = 0;
            for (int i = 0; i < c->calls_count; ++i){
                if (c->calls[i] == call){
                    call_found = 1;
                    c->calls[i] = c->calls[c->calls_count-1];
                    c->calls[c->calls_count-1] = NULL;
                }
            }
            char j = 0;
            if (call_found){
                for (int i =0; i < call->count; ++i){
                    Connection* conn = Connection_find(call->participants[i]);
                    struct NBSSL_Buffer *obuf = &conn->ssl_out;
                    char q = CALLENDSERVER;
                    sslbuf_clean(obuf);
                    sslbuf_write(obuf, &q, 1);
                    sslbuf_write(obuf, callname, CALL_NAME_SZ);
                    handle_callend_server(conn);
                    if (call->participants[i] == c->client_fd)
                        j = 1;
                    else
                        call->participants[i-j] = call->participants[i];
                }
                call->count -= 1;
            }
            else{
                struct NBSSL_Buffer* buf2 = &c->ssl_out;
                sslbuf_clean(buf2);
                struct ErrorInfo info = {
                    .command = ERRORUNKNOWNSERVER,
                    .previous_command = c->status,
                    .size = htonl((uint32_t) buf->seekend)};
                memcpy(info.data, buf->data, buf->seekend);
                sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
                sslbuf_clean(buf);
                return handle_errorunknown_server(c);
            }
            break;
        }
        case ISNT_COMPLETED:{
            c->status = CALLENDCLIENT;
            break;
        }
    }
    return ret;
}

inline static int handle_calljoin_client(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    if (c->status != CALLJOINCLIENT){
        sslbuf_clean(buf);
        c->status = CALLJOINCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, &c->ssl_in);
    switch(ret){
        case CAUGHT_ERROR:{
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htonl((uint32_t) buf->seekend)};
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
            
        }
        case IS_COMPLETED:{
            c->status = DOING_NOTHING;
            char callname[CALL_NAME_SZ];
            sslbuf_read(buf, callname, CALL_NAME_SZ);
            Call* call = Call_find(callname);
            for (int i = 0; i < call->count; ++i){
                Connection *conn = Connection_find(call->participants[i]);
                struct NBSSL_Buffer *obuf = &conn->ssl_out;
                struct CallMemberInfo info = {
                    .command = CALLNEWMEMBERSERVER,
                    .member_id = htonl((uint32_t) c->client_fd)
                };
                memcpy(info.callname, callname, CALL_NAME_SZ);
                sslbuf_clean(obuf);
                sslbuf_write(obuf, &info, sizeof(info));
                handle_callnewmember_server(conn);
            }
            struct NBSSL_Buffer *obuf = &c->ssl_out;
                
            struct CallFullInfo info = {
                .command = CALLJOINSERVER,
                .participants_count = htonl((uint32_t) call->count)
            };
            memcpy(info.callname, callname, CALL_NAME_SZ);
            memcpy(info.sym_key, call->symm_key, SYMM_KEY_LEN);
            for (int i = 0; i < call->count; ++i){
                info.participants[i] = htonl((uint32_t) call->participants[i]);
            }
            sslbuf_clean(obuf);
            sslbuf_write(obuf, &info, 1+CALL_NAME_SZ+SYMM_KEY_LEN+1+call->count*sizeof(int));
            handle_calljoin_server(c);
            c->calls[c->calls_count] = call;
            c->calls_count++;
            call->participants[call->count] = c->client_fd;
            call->count++;
            break;
        }
        case ISNT_COMPLETED:{
            c->status = CALLJOINCLIENT;
            break;
        }
    }
    return ret;
}

inline static int handle_callnewmember_server(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    
    struct NBSSL_Buffer* buf = &c->ssl_out;
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret){
        case CAUGHT_ERROR:
        {
            Connection_delete_with_disconnectfromcalls(c);
            break;
        }
        case IS_COMPLETED:
        {
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        }
        case ISNT_COMPLETED:
        {
            c->status = CALLNEWMEMBERSERVER;
        }
    }
    return ret;
}

inline static int handle_callleave_client(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    if (c->status != CALLLEAVECLIENT){
        sslbuf_clean(buf);
        c->status = CALLLEAVECLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, &c->ssl_in);
    switch(ret){
        case CAUGHT_ERROR:{
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htonl((uint32_t) buf->seekend)};
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
            
        }
        case IS_COMPLETED:{
            c->status = DOING_NOTHING;
            char callname[CALL_NAME_SZ];
            int fd = 0;
            char b = 0;
            sslbuf_read(buf, callname, CALL_NAME_SZ);
            sslbuf_read(buf, &fd, sizeof(int));
            fd = ntohl(fd);
            sslbuf_read(buf, &b, 1);
            Call *call = Call_find(callname);
            if (*call->participants == fd){
                for (int j = 0; j < call->count; ++j){
                    int participant = call->participants[j];
                    Connection* conn = Connection_find(participant);
                    struct NBSSL_Buffer *obuf = &conn->ssl_out;
                    sslbuf_clean(obuf);
                    char q = CALLLEAVEOWNERSERVER;
                    sslbuf_write(obuf, &q, 1);
                    sslbuf_write(obuf, call->callname, CALL_NAME_SZ);
                    handle_callleaveowner_server(conn);
                }
                Call_delete(call);
            }
            else{
                for (int j = 0; j < call->count; ++j){
                    int participant = call->participants[j];
                    Connection* conn = Connection_find(participant);
                    struct NBSSL_Buffer * obuf = &conn->ssl_out;
                    sslbuf_clean(obuf);
                    char q = CALLLEAVESERVER;
                    sslbuf_write(obuf, &q, 1);
                    sslbuf_write(obuf, call->callname, CALL_NAME_SZ);
                    int a = htonl((uint32_t) fd);
                    sslbuf_write(obuf, &a, sizeof(a));
                    sslbuf_write(obuf, &b, 1);
                    handle_callleave_server(conn);
                }
                if (b < 1)
                    Call_delete(call);       
            }
            break;
        }
        case ISNT_COMPLETED:{
            c->status = CALLLEAVECLIENT;
            break;
        }
    }
    return ret;


}

inline static int handle_errorunknown_client(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    if (c->status != ERRORUNKNOWNCLIENT){
        sslbuf_clean(buf);
        c->status = ERRORUNKNOWNCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, &c->ssl_in);
    switch(ret){
        case CAUGHT_ERROR:{
            Connection_delete_with_disconnectfromcalls(c);
            break;
        }
        case IS_COMPLETED:{
            c->status = DOING_NOTHING;
            break;
        }
        case ISNT_COMPLETED:{
            c->status = ERRORUNKNOWNCLIENT;
            break;
        }
    }
    return ret;


}

inline static int handle_udp_packet(){


}

inline static int handle_listen(){

    struct sockaddr_storage peer;
    socklen_t plen = sizeof(peer);
    int client_fd;

#ifdef SOCK_NONBLOCK
    client_fd = accept4(tfd, (struct sockaddr*)&peer, &plen, SOCK_NONBLOCK);
#else
    client_fd = accept(tfd, (struct sockaddr*)&peer, &plen);
    if (client_fd >= 0) {
        if (set_nonblocking(client_fd) == -1) {
            perror("set_nonblocking(accepted)");
            close(client_fd);
            return;
        }
    }
#endif

    if (client_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) 
            // всё фигня, давай по новой
            return;
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

    SSL *ssl = SSL_new(ssl_ctx);
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

    int ret = NB_SSL_accept(ssl, client_fd, &c->flags);
    switch(ret){
        case CAUGHT_ERROR:{
            Connection_destruct(c);
            return;
        }
        case IS_COMPLETED:{
            c->status = DOING_NOTHING;
            break;
        }
        case ISNT_COMPLETED:{
            c->status = SSL_ACCEPTING;
            break;
        }
    }
    Connection_add(c); 
    // Логируем
    char addrbuf[INET_ADDRSTRLEN] = "<unknown>";
    if (peer.ss_family == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in*)&peer;
        inet_ntop(AF_INET, &sa->sin_addr, addrbuf, sizeof(addrbuf));
    }
    if (ret == IS_COMPLETED)
        fprintf(stderr, "Accepted client %d from %s (status: %s)\n",
            client_fd, addrbuf, status_to_str(c->status));

    // Если handshake закончен сразу — вы, возможно, хотите тут же читать
    // первый защищённый пакет (например, чтобы клиент сразу прислал UDP-порт).
    // Я не делаю чтение здесь, оставляю это на handle_client при EPOLLIN:
    //   handle_client() должен проверять c->ssl и вызывать SSL_read,
    //   а при получении специального сообщения обновлять c->udp_addr.sin_port.
}

inline static int handle_unfinished_handshake(Connection *c){
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    int ret = NB_SSL_accept(ssl, fd, &c->flags);
    switch(ret){
        case CAUGHT_ERROR:{
            Connection_delete(c);
            return;
        }
        case IS_COMPLETED:{
            c->status = DOING_NOTHING;
            if (ret == IS_COMPLETED)
                fprintf(stderr, "Accepted client %d (status: %s)\n",
                    fd, status_to_str(c->status));
            break;
        }
        case ISNT_COMPLETED:{
            c->status = SSL_ACCEPTING;
            break;
        }
    }
}

inline static int handle_new_command(Connection *c){
    c->status = DOING_NOTHING;
    SSL *ssl = c->ssl;
    int fd = c->client_fd;
    unsigned char opcode;
    int n = SSL_read(ssl, &opcode, 1);
    
    if (n <= 0) {
        int ssl_err = SSL_get_error(ssl, n);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            return;
        }
        if (ssl_err == SSL_ERROR_WANT_WRITE){
            change_flags(fd, &c->flags, SENDING);
            return;
        }

        Connection_delete_with_disconnectfromcalls(c);
        return;
    }

    switch (opcode) {
        case CALLSTARTCLIENT:
            /* Клиент запрашивает создание конференции (payload = только 1 байт) */
            handle_callstart_server(c);
            break;
        case CALLENDCLIENT:
            /* После байта идёт имя конференции — обработчик должен прочитать оставшиеся данные (через SSL_read) */
            handle_callend_server(c);
            break;
        case CALLJOINCLIENT:
            handle_calljoin_server(c);
            break;
        case CALLLEAVECLIENT:
            handle_callleave_server(c);
            break;
        case ERRORUNKNOWNCLIENT:
            handle_errorunknown_server(c);
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

// логика будет ломаться в случае, если человек находится в нескольких конференциях
// и другой человек находится в этих же конференциях
// и первый человек ливает из этих конференций
inline static int handle_client(int fd){
    Connection *c = Connection_find(fd);
    if (!c) {
        /* Неожиданно — нет такого connection: удалить из epoll и закрыть */
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return;
    }
    SSL *ssl = c->ssl;
    if (!ssl) {
        /* Нет SSL — считаем это некорректным, закрываем */
        Connection_delete(c);
        return;
    }
    
    switch(c->status){
        case DOING_NOTHING:
        {
            handle_new_command(c);
            break;
        }
        case SSL_ACCEPTING:
        {
            handle_unfinished_handshake(c);
            break;
        }
        case INFOCLIENT:
        {
            handle_info_client(c);
            break;
        }
        case CALLSTARTCLIENT:
        {
            handle_callstart_client(c);
            break;
        }
        case CALLENDCLIENT:
        {
            handle_callend_client(c);
            break;
        }
        case CALLJOINCLIENT:
        {
            handle_calljoin_client(c);
            break;
        }
        case CALLLEAVECLIENT:
        {
            handle_callleave_client(c);
            break;
        }
        case ERRORUNKNOWNCLIENT:
        {
            handle_errorunknown_client(c);
            break;
        }
        case ERRORUNKNOWNSERVER:
        {
            handle_errorunknown_server(c);
            break;
        }
        /*
TODO если в результате уменьшения числа участников

        case ERRORCALLJOINMEMBER:
        {
            handle_errorcalljoinmember(c);
            break;
        }
        case ERRORCALLNOTMEMBER:
        {
            handle_errorcallnotmember(c);
            break;
        }
        case ERRORCALLNOPERMISSIONS:
        {
            handle_errorcallnopermissions(c);
            break;
        }
        case ERRORCALLNOTOWNER:
        {
            handle_errorcallnotowner(c);
            break;
        }
        case ERRORCALLNOTEXISTS:
        {
            handle_errorcallnotexists(c);
            break;
        }
        case CALLSTARTSERVER:
        {
            handle_callstart_server(c);
            break;
        }
        case CALLENDSERVER:
        {
            handle_callend_server(c);
            break;
        }
        case CALLJOINSERVER:
        {
            handle_calljoin_server(c);
            break;
        }
        case CALLNEWMEMBERSERVER:
        {
            handle_callnewmember_server(c);
            break;
        }
        case CALLLEAVESERVER:
        {
            handle_callleave_server(c);
            break;
        }
        case CALLLEAVEOWNERSERVER:
        {
            handle_callleaveowner_server(c);
            break;
        }*/
    }


    /* 1) Если handshake ещё не завершён — продолжить его (non-blocking)/
    if (!SSL_is_init_finished(ssl)) 
        handle_unfinished_handshake(c);
    else
        handle_ordinary_connection(c);
        */
}


int main(){
    MainState_construct();
    int status = 0;
    if (status = simple_starting_actions())
        goto State_destruct;
    if (status = init_ssl_ctx())
        goto State_destruct;
    if (status = tcp_socket_configuration())
        goto SSL_cleanup;
    if (status = udp_socket_configuration())
        goto SSL_cleanup;
    if (status = epfd_configuration())
        goto SSL_cleanup;

    struct epoll_event events[MAX_EVENTS];
    while (!stop_requested){
	    int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
	    for (int i = 0; i < n; ++i){
	    	int fd = events[i].data.fd;
		    if (likely(fd == ufd))
                handle_udp_packet();
		    else if (unlikely(fd == tfd))
                handle_listen();
		    else 
                handle_client(fd);
	    }
    }

SSL_cleanup:
    cleanup_ssl_ctx();
State_destruct:
    MainState_destruct();
    return status;
}
        
// проходимся по всем конференциям, в которых был этот чел, и отправляем CALLLEAVESERVER/CALLLEAVEOWNERSERVER
inline static void Connection_delete_with_disconnectfromcalls(Connection* self){
    for (int i = 0; i< self->calls_count; ++i){
        Call* call=self->calls[i];
        if (*call->participants == self->client_fd){
            for (int j = 1; j < call->count; ++j){
                int participant = call->participants[j];
                Connection* conn = Connection_find(participant);
                struct NBSSL_Buffer * obuf = &conn->ssl_out;
                sslbuf_clean(obuf);
                char q = CALLENDSERVER;
                sslbuf_write(obuf, &q, 1);
                sslbuf_write(obuf, call->callname, CALL_NAME_SZ);
                handle_callend_server(conn);
            }
            Call_delete(call);
        }
        else{
            char b = call->count-1;
            for (int j = 0; j < call->count; ++j){
                int participant = call->participants[j];
                Connection* conn = Connection_find(participant);
                struct NBSSL_Buffer * obuf = &conn->ssl_out;
                sslbuf_clean(obuf);
                char q = CALLLEAVESERVER;
                sslbuf_write(obuf, &q, 1);
                sslbuf_write(obuf, call->callname, CALL_NAME_SZ);
                int a = htonl((uint32_t) self->client_fd);
                sslbuf_write(obuf, &a, sizeof(a));
                sslbuf_write(obuf, &b, 1);
                handle_callleave_server(conn);
            }
            if (b < 1)
                Call_delete(call);       
        }
    }
    Connection_delete(self);
}