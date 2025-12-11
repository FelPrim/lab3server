#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>

// Изменяем на объявления (extern) вместо определений
extern int g_udp_fd;
extern int g_tcp_fd; 
extern int g_epoll_fd;

// Определяем SOCK_NONBLOCK если не определен (для совместимости)
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK O_NONBLOCK
#endif

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    
    // Устанавливаем неблокирующий режим
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        return -1;
    }
    
    return 0;
}

int create_tcp_server(int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    // Создаем TCP сокет
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket failed");
        return -1;
    }

    // Сразу устанавливаем неблокирующий режим для серверного сокета
    if (set_nonblocking(server_fd) == -1) {
        close(server_fd);
        return -1;
    }

    // Устанавливаем опции сокета
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        close(server_fd);
        return -1;
    }

    // Настраиваем адрес
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Биндим сокет
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    // Начинаем слушать
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    printf("TCP server created on port %d, fd: %d\n", port, server_fd);
    return server_fd;
}

int create_udp_server(int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    // Создаем UDP сокет
    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket failed");
        return -1;
    }

    // Сразу устанавливаем неблокирующий режим
    if (set_nonblocking(server_fd) == -1) {
        close(server_fd);
        return -1;
    }

    // Устанавливаем опции сокета
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        close(server_fd);
        return -1;
    }

    // Настраиваем адрес
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Биндим сокет
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    printf("UDP server created on port %d, fd: %d\n", port, server_fd);
    return server_fd;
}

int create_epoll_fd(void) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return -1;
    }
    printf("Epoll fd created: %d\n", epoll_fd);
    return epoll_fd;
}

int epoll_add(int epoll_fd, int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl ADD");
        return -1;
    }
    printf("Added fd %d to epoll %d with events 0x%x\n", fd, epoll_fd, events);
    return 0;
}

int epoll_modify(int epoll_fd, int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        perror("epoll_ctl MOD");
        return -1;
    }
    printf("Modified fd %d in epoll %d with events 0x%x\n", fd, epoll_fd, events);
    return 0;
}

int epoll_remove(int epoll_fd, int fd) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl DEL");
        return -1;
    }
    printf("Removed fd %d from epoll %d\n", fd, epoll_fd);
    return 0;
}

int accept_connection(int server_fd, struct sockaddr_in* client_addr) {
    socklen_t client_len = sizeof(struct sockaddr_in);
    
    // Используем стандартный accept
    int client_fd = accept(server_fd, (struct sockaddr*)client_addr, &client_len);
    
    if (client_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Нет ожидающих соединений - это нормально для неблокирующего сокета
            return -2;
        }
        perror("accept");
        return -1;
    }

    // Устанавливаем неблокирующий режим для клиентского сокета
    if (set_nonblocking(client_fd) == -1) {
        close(client_fd);
        return -1;
    }

    // Устанавливаем TCP_NODELAY для уменьшения задержки
    int yes = 1;
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        perror("setsockopt TCP_NODELAY");
        // Не фатальная ошибка, продолжаем
    }

    printf("Accepted connection from %s:%d, fd: %d\n",
           inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), client_fd);
    return client_fd;
}

int udp_send_packet(int udp_fd, const void* data, size_t len,
                   const struct sockaddr_in* dest_addr) {
    
    // Используем MSG_DONTWAIT для неблокирующей отправки
    ssize_t sent = sendto(udp_fd, data, len, MSG_DONTWAIT,
                         (const struct sockaddr*)dest_addr, sizeof(*dest_addr));
    //printf("sent: %ld; udp_fd=%d, len=%zu, dest_addr={family=%d, addr=%s, port=%d}\n", 
    //   sent, udp_fd, len, 
    //   dest_addr->sin_family,
    //   inet_ntoa(((struct sockaddr_in*)dest_addr)->sin_addr),
    //   ntohs(((struct sockaddr_in*)dest_addr)->sin_port));
    if (sent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Буфер отправки заполнен - нужно повторить позже
            return -2;
        }
        perror("sendto");
        return -1;
    }

    if ((size_t)sent != len) {
        fprintf(stderr, "Partial UDP send: %zd of %zu bytes\n", sent, len);
        return -1;
    }

    return (int)sent;
}

int udp_receive_packet(int udp_fd, void* buffer, size_t buffer_len,
                      struct sockaddr_in* src_addr) {
    socklen_t addr_len = sizeof(struct sockaddr_in);
    
    // Используем MSG_DONTWAIT для неблокирующего приема
    ssize_t received = recvfrom(udp_fd, buffer, buffer_len, MSG_DONTWAIT,
                               (struct sockaddr*)src_addr, &addr_len);
    
    if (received == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Нет данных - это нормально для неблокирующего сокета
            return -2;
        }
        perror("recvfrom");
        return -1;
    }

    if (received == 0) {
        // UDP обычно не возвращает 0, но на всякий случай
        return 0;
    }

    return (int)received;
}

void sockaddr_to_string(const struct sockaddr_in* addr, char* buffer, size_t len) {
    if (addr && buffer) {
        const char* ip = inet_ntoa(addr->sin_addr);
        uint16_t port = ntohs(addr->sin_port);
        snprintf(buffer, len, "%s:%d", ip, port);
    }
}

int compare_sockaddr(const struct sockaddr_in* a, const struct sockaddr_in* b) {
    if (!a || !b) return -1;
    if (a->sin_addr.s_addr != b->sin_addr.s_addr) return 1;
    if (a->sin_port != b->sin_port) return 1;
    return 0;
}

// Асинхронное чтение из сокета
int async_read(int fd, void* buffer, size_t buffer_len) {
    ssize_t received = read(fd, buffer, buffer_len);
    
    if (received == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -2; // Нет данных сейчас
        }
        perror("async_read");
        return -1; // Ошибка
    }
    
    if (received == 0) {
        return 0; // Соединение закрыто
    }
    
    return (int)received; // Успешно прочитано
}

// Асинхронная запись в сокет
int async_write(int fd, const void* data, size_t len) {
    ssize_t sent = write(fd, data, len);
    
    if (sent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -2; // Буфер заполнен, нужно повторить позже
        }
        perror("async_write");
        return -1; // Ошибка
    }
    
    if ((size_t)sent != len) {
        return -3; // Частичная запись
    }
    
    return (int)sent; // Успешно записано
}