#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

extern int g_epoll_fd;
extern int g_tcp_fd;
extern int g_udp_fd;

// Настройка неблокирующего сокета
int set_nonblocking(int fd);

// Создание и настройка серверных сокетов
int create_tcp_server(int port);
int create_udp_server(int port);

// Функции для epoll
int create_epoll_fd(void);
int epoll_add(int epoll_fd, int fd, uint32_t events);
int epoll_modify(int epoll_fd, int fd, uint32_t events);
int epoll_remove(int epoll_fd, int fd);

// Принятие нового соединения (неблокирующее)
int accept_connection(int server_fd, struct sockaddr_in* client_addr);

// Отправка/прием UDP пакетов
int udp_send_packet(int udp_fd, const void* data, size_t len,
                   const struct sockaddr_in* dest_addr);
int udp_receive_packet(int udp_fd, void* buffer, size_t buffer_len,
                      struct sockaddr_in* src_addr);

// Асинхронные операции ввода-вывода
int async_read(int fd, void* buffer, size_t buffer_len);
int async_write(int fd, const void* data, size_t len);

// Утилиты для работы с адресами
void sockaddr_to_string(const struct sockaddr_in* addr, char* buffer, size_t len);
int compare_sockaddr(const struct sockaddr_in* a, const struct sockaddr_in* b);