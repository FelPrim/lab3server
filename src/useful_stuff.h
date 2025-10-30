#ifndef USEFUL_STUFF_H
#define USEFUL_STUFF_H

#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netdb.h>

// Макросы для подсказок ветвления
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

// Статусы операций
#define IS_COMPLETED    0
#define ISNT_COMPLETED  1
#define CAUGHT_ERROR   -1

// Флаги для epoll
#define UNINITIALIZED   0
#define RECVING         (EPOLLIN | EPOLLRDHUP)
#define SENDING         (EPOLLIN | EPOLLRDHUP | EPOLLOUT)

// Внешние объявления глобальных переменных
extern int epfd;
extern int tfd;
extern int ufd;

#endif // USEFUL_STUFF_H
