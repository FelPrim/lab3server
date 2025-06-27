#pragma once

#ifndef __linux__
    #error This code was written to run only on linux
#endif
#include "useful_stuff.h"
#include "protocol.h"
// TODO вырезать лишние библиотеки
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

#include <openssl/ssl.h>
#include <openssl/err.h>

//inline static int handshake_established(const SSL* ssl){
//    return SSL_is_init_finished(ssl);
//}



#define UNINITIALIZED 0
#define RECVING (EPOLLIN | EPOLLRDHUP)
#define SENDING (EPOLLIN | EPOLLRDHUP | EPOLLOUT)


inline static int change_flags(int fd, int *old_flags, int new_flags){
    int epoll_mode;
    if (*old_flags == UNINITIALIZED)
        epoll_mode = EPOLL_CTL_ADD;
    else
        epoll_mode = EPOLL_CTL_MOD;
    struct epoll_event ev = {
        .data.fd = fd,
        .events = new_flags
    };
    *old_flags = new_flags;
    return epoll_ctl(epfd, epoll_mode, fd, &ev);
}

inline static int NB_SSL_accept(SSL *ssl, int fd, int *flags){
    int ret = SSL_accept(ssl);
    if (ret == 1){
        change_flags(fd, flags, RECVING);
        return IS_COMPLETED;
    }
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ){
        change_flags(fd, flags, RECVING);
        return ISNT_COMPLETED;
    }
    if (err == SSL_ERROR_WANT_WRITE){
        change_flags(fd, flags, SENDING);
        return ISNT_COMPLETED;
    }
    fprintf(stderr, "SSL accept error: %d\n", err);
    return CAUGHT_ERROR;
}
 
struct NBSSL_Buffer{
    char data[MAXTLSSZ];
    int seek;
    int seekend;
};

inline static void sslbuf_clean(struct NBSSL_Buffer* self){
    self->seek = 0;
    self->seekend = 0;
}

inline static void* sslbuf_write(struct NBSSL_Buffer* self, const void* src, size_t n){
    self->seekend = self->seek+n;
    assert(self->seekend < 2048);
    void* ptr = memcpy(self->data+self->seek, src, n);
    self->seek += n;
    return ptr;
}

inline static int sslbuf_read(struct NBSSL_Buffer* self, void* dest, size_t n){
    assert(n+self->seek <= self->seekend);
    void* ptr = memcpy(dest, self->data+self->seek, n);
    self->seek += n;
    if (self->seek < self->seekend)
        return ISNT_COMPLETED;
    sslbuf_clean(self);
    return IS_COMPLETED;
}

// sslbuf_clean -> sslbuf_write -> NB_SSL_write
inline static int NB_SSL_write(SSL *ssl, int fd, int *flags, struct NBSSL_Buffer *buf){
    int ret = SSL_write(ssl, buf->data+buf->seek, buf->seekend - buf->seek);
    if (ret > 0){
        buf->seek += ret;
        if (buf->seek >= buf->seekend){
            change_flags(fd, flags, RECVING);
            sslbuf_clean(buf);
            return IS_COMPLETED;
        }
        return ISNT_COMPLETED;
    }
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ)
        return ISNT_COMPLETED;

    fprintf(stderr, "SSL write error: %d\n", err);
    return CAUGHT_ERROR;
}

// sslbuf_clean -> NB_SSL_read -> sslbuf_read
inline static int NB_SSL_read(SSL *ssl, int fd, int *flags, struct NBSSL_Buffer *buf){
    int ret = SSL_read(ssl, buf->data+buf->seek, buf->seekend - buf->seek);
    if (ret > 0){
        buf->seek += ret;
        if (buf->seek >= buf->seekend){
            buf->seekend = buf->seek;
            buf->seek = 0;
            return IS_COMPLETED;
        }
        return ISNT_COMPLETED;
    }
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ)
        return ISNT_COMPLETED;

    fprintf(stderr, "SSL read error: %d\n", err);
    return CAUGHT_ERROR;
}
