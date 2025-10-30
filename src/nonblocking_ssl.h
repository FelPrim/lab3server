#pragma once

#include <openssl/ssl.h>
#include <assert.h>
#include <string.h>

#include "protocol.h"
#include "useful_stuff.h"

// Структура буфера для неблокирующих SSL операций
struct NBSSL_Buffer {
    unsigned char data[MAXTLSSZ];
    int seek;
    int seekend;
};

// Инициализация буфера
inline static void sslbuf_clean(struct NBSSL_Buffer* self) {
    if (self) {
        self->seek = 0;
        self->seekend = 0;
        memset(self->data, 0, MAXTLSSZ);
    }
}

// Запись в буфер с проверкой границ
inline static void* sslbuf_write(struct NBSSL_Buffer* self, const void* src, size_t n) {
    if (!self || !src || self->seek + n > MAXTLSSZ) {
        return NULL;
    }
    
    void* ptr = memcpy(self->data + self->seek, src, n);
    self->seek += n;
    self->seekend = self->seek;  // Обновляем конец данных
    
    return ptr;
}

// Чтение из буфера с проверкой границ
inline static int sslbuf_read(struct NBSSL_Buffer* self, void* dest, size_t n) {
    if (!self || !dest || self->seek + n > self->seekend) {
        return CAUGHT_ERROR;
    }
    
    memcpy(dest, self->data + self->seek, n);
    self->seek += n;
    
    if (self->seek >= self->seekend) {
        sslbuf_clean(self);
        return IS_COMPLETED;
    }
    
    return ISNT_COMPLETED;
}

// Изменение флагов epoll для файлового дескриптора
inline static int change_flags(int fd, int *old_flags, int new_flags) {
    if (fd < 0 || !old_flags) return -1;
    
    int epoll_mode;
    if (*old_flags == UNINITIALIZED) {
        epoll_mode = EPOLL_CTL_ADD;
    } else {
        epoll_mode = EPOLL_CTL_MOD;
    }
    
    struct epoll_event ev = {
        .data.fd = fd,
        .events = new_flags
    };
    
    if (epoll_ctl(epfd, epoll_mode, fd, &ev) == -1) {
        return -1;
    }
    
    *old_flags = new_flags;
    return 0;
}

// Неблокирующее SSL handshake (accept)
inline static int NB_SSL_accept(SSL *ssl, int fd, int *flags) {
    if (!ssl || fd < 0 || !flags) return CAUGHT_ERROR;
    
    int ret = SSL_accept(ssl);
    if (ret == 1) {
        change_flags(fd, flags, RECVING);
        return IS_COMPLETED;
    }
    
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ) {
        change_flags(fd, flags, RECVING);
        return ISNT_COMPLETED;
    }
    if (err == SSL_ERROR_WANT_WRITE) {
        change_flags(fd, flags, SENDING);
        return ISNT_COMPLETED;
    }
    
    return CAUGHT_ERROR;
}

// Неблокирующая запись SSL
inline static int NB_SSL_write(SSL *ssl, int fd, int *flags, struct NBSSL_Buffer *buf) {
    if (!ssl || fd < 0 || !flags || !buf) return CAUGHT_ERROR;
    
    int ret = SSL_write(ssl, buf->data + buf->seek, buf->seekend - buf->seek);
    if (ret > 0) {
        buf->seek += ret;
        if (buf->seek >= buf->seekend) {
            change_flags(fd, flags, RECVING);
            sslbuf_clean(buf);
            return IS_COMPLETED;
        }
        return ISNT_COMPLETED;
    }
    
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
        return ISNT_COMPLETED;
    }
    
    return CAUGHT_ERROR;
}

// Неблокирующее чтение SSL
inline static int NB_SSL_read(SSL *ssl, int fd, int *flags, struct NBSSL_Buffer *buf) {
    if (!ssl || fd < 0 || !flags || !buf) return CAUGHT_ERROR;
    
    // Вычисляем, сколько можем прочитать
    int available = MAXTLSSZ - buf->seekend;
    if (available <= 0) {
        return CAUGHT_ERROR;  // Буфер полон
    }
    
    int ret = SSL_read(ssl, buf->data + buf->seekend, available);
    if (ret > 0) {
        buf->seekend += ret;
        
        // Добавляем нуль-терминатор для безопасности (если есть место)
        if (buf->seekend < MAXTLSSZ) {
            buf->data[buf->seekend] = '\0';
        }
        
        return ISNT_COMPLETED;  // Чтение продолжается
    }
    
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        return ISNT_COMPLETED;
    }
    
    return CAUGHT_ERROR;
}

