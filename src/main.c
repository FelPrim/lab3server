// main.c - основной файл сервера
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include "network.h"
#include "connection.h"
#include "protocol.h"
#include "buffer_logic.h"  // Добавлено для функций buffer_protocol_*

#define MAX_EVENTS 64
#define BUFFER_READ_SIZE 4096

// Глобальные переменные для сокетов
extern int g_tcp_fd;
extern int g_udp_fd;
extern int g_epoll_fd;

volatile sig_atomic_t stop_requested = 0;

void handle_signal(int sig) {
    stop_requested = 1;
    printf("\nReceived signal %d, shutting down...\n", sig);
}

void handle_tcp_connection(int epoll_fd, int tcp_fd) {
    struct sockaddr_in client_addr;
    int client_fd = accept_connection(tcp_fd, &client_addr);
    
    if (client_fd > 0) {
        // Создаем и добавляем новое соединение
        Connection* conn = connection_create(client_fd, &client_addr);
        if (conn) {
            connection_add(conn);
            
            // Добавляем клиентский сокет в epoll для чтения (только EPOLLIN изначально)
            if (epoll_add(epoll_fd, client_fd, EPOLLIN | EPOLLET) < 0) {
                fprintf(stderr, "Failed to add client fd %d to epoll\n", client_fd);
                connection_remove(client_fd);
            } else {
                printf("New connection: fd=%d, %s\n", client_fd, connection_get_address_string(conn));
            }
        } else {
            close(client_fd);
        }
    } else if (client_fd == -1) {
        perror("accept_connection failed");
    }
    // client_fd == -2 означает EAGAIN/EWOULDBLOCK - нет новых соединений
}

void handle_udp_socket(int udp_fd) {
    uint8_t buffer[1500];
    struct sockaddr_in src_addr;
    
    while (1) {
        int len = udp_receive_packet(udp_fd, buffer, sizeof(buffer), &src_addr);
        
        if (len > 0) {
            // Обрабатываем UDP пакет
            handle_udp_packet(buffer, len, &src_addr);
        } else if (len == -2) {
            // EAGAIN/EWOULDBLOCK - больше нет данных
            break;
        } else if (len == -1) {
            // Ошибка
            perror("udp_receive_packet failed");
            break;
        } else if (len == 0) {
            // Неожиданный конец данных для UDP
            break;
        }
    }
}

void handle_client_read(int epoll_fd, Connection* conn) {
    // Читаем данные от клиента
    int result = connection_read_data(conn);
    
    if (result > 0) {
        Buffer* read_buf = &conn->read_buffer;
        
        // ОТЛАДОЧНЫЙ ВЫВОД: покажем что получили
        printf("DEBUG: Received %d bytes, buffer position: %u, expected_size: %u\n",
               result, read_buf->position, read_buf->expected_size);
        
        if (read_buf->position > 0) {
            printf("DEBUG: First byte (message type): 0x%02x\n", read_buf->data[0]);
        }
        
        while (buffer_protocol_state(read_buf) == BUFFER_IS_COMPLETE) {
            uint8_t message_type = read_buf->data[0];
            size_t payload_len = read_buf->expected_size - 1;
            const uint8_t* payload = payload_len > 0 ? read_buf->data + 1 : NULL;
            
            printf("Processing message from fd=%d: type=0x%02x, len=%zu\n", 
                   conn->fd, message_type, payload_len);
            
            // ОТЛАДОЧНЫЙ ВЫВОД для CLIENT_UDP_ADDR
            if (message_type == CLIENT_UDP_ADDR && payload_len >= sizeof(UDPAddrFullPayload)) {
                const UDPAddrFullPayload* udp_payload = (const UDPAddrFullPayload*)payload;
                printf("DEBUG: CLIENT_UDP_ADDR payload - family: 0x%04x, port: %u, ip: 0x%08x\n",
                       udp_payload->family, ntohs(udp_payload->port), ntohl(udp_payload->ip));
            }
            
            handle_client_message(conn, message_type, payload, payload_len);
            
            buffer_protocol_consume(read_buf);
            
            if (read_buf->position > 0) {
                buffer_protocol_set_expected(read_buf);
            }
        }
    } else if (result == 0) {
        // Соединение закрыто клиентом
        printf("Client disconnected: fd=%d\n", conn->fd);
        handle_connection_closed(conn);
        epoll_remove(epoll_fd, conn->fd);
        connection_remove(conn->fd);
    } else if (result == -1) {
        // Ошибка чтения
        printf("Error reading from client: fd=%d\n", conn->fd);
        handle_connection_closed(conn);
        epoll_remove(epoll_fd, conn->fd);
        connection_remove(conn->fd);
    }
}

void handle_client_write(Connection* conn) {
    
    // Пытаемся отправить данные из буфера записи
    if (buffer_get_data_size(&conn->write_buffer) > 0) {
        int result = connection_write_data(conn);
        
        if (result == 1) {
            // Все данные записаны
            printf("All data written to client: fd=%d\n", conn->fd);
        } else if (result == -1) {
            // Ошибка записи
            printf("Error writing to client: fd=%d\n", conn->fd);
        }
        // result == -2 означает EAGAIN/EWOULDBLOCK - продолжаем позже
    }
    
    // Убираем EPOLLOUT из мониторинга если буфер записи пуст
    if (buffer_get_data_size(&conn->write_buffer) == 0) {
        epoll_modify(g_epoll_fd, conn->fd, EPOLLIN | EPOLLET);
    }
}

void handle_client_event(int epoll_fd, int client_fd, uint32_t events) {
    Connection* conn = connection_find(client_fd);
    if (!conn) {
        fprintf(stderr, "Connection not found for fd %d\n", client_fd);
        epoll_remove(epoll_fd, client_fd);
        close(client_fd);
        return;
    }
    
    // Обрабатываем события чтения
    if (events & EPOLLIN) {
        handle_client_read(epoll_fd, conn);
    }
    
    // Обрабатываем события записи
    if (events & EPOLLOUT) {
        handle_client_write(conn);
    }
    
    // Обрабатываем ошибки и разрыв соединения
    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        printf("Client connection error/close: fd=%d, events=0x%x\n", client_fd, events);
        handle_connection_closed(conn);
        epoll_remove(epoll_fd, client_fd);
        connection_remove(client_fd);
    }
}

void setup_signal_handlers(void) {
    // Используем простой signal() вместо sigaction для совместимости
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Игнорируем SIGPIPE чтобы не падать при записи в закрытый сокет
    signal(SIGPIPE, SIG_IGN);
}

int initialize_servers(void) {
    printf("Initializing servers...\n");
    
    // Создаем TCP сервер
    g_tcp_fd = create_tcp_server(23231);
    if (g_tcp_fd < 0) {
        fprintf(stderr, "Failed to create TCP server\n");
        return -1;
    }
    
    // Создаем UDP сервер  
    g_udp_fd = create_udp_server(23230);
    if (g_udp_fd < 0) {
        fprintf(stderr, "Failed to create UDP server\n");
        close(g_tcp_fd);
        return -1;
    }
    
    // Создаем epoll
    g_epoll_fd = create_epoll_fd();
    if (g_epoll_fd < 0) {
        fprintf(stderr, "Failed to create epoll\n");
        close(g_tcp_fd);
        close(g_udp_fd);
        return -1;
    }
    
    // Добавляем серверные сокеты в epoll
    if (epoll_add(g_epoll_fd, g_tcp_fd, EPOLLIN) < 0) {
        fprintf(stderr, "Failed to add TCP socket to epoll\n");
        close(g_tcp_fd);
        close(g_udp_fd);
        close(g_epoll_fd);
        return -1;
    }
    
    if (epoll_add(g_epoll_fd, g_udp_fd, EPOLLIN) < 0) {
        fprintf(stderr, "Failed to add UDP socket to epoll\n");
        close(g_tcp_fd);
        close(g_udp_fd);
        close(g_epoll_fd);
        return -1;
    }
    
    printf("Servers initialized successfully\n");
    return 0;
}

void cleanup_servers(void) {
    printf("Cleaning up servers...\n");
    
    if (g_epoll_fd >= 0) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }
    
    if (g_tcp_fd >= 0) {
        close(g_tcp_fd);
        g_tcp_fd = -1;
    }
    
    if (g_udp_fd >= 0) {
        close(g_udp_fd);
        g_udp_fd = -1;
    }
    
    connection_close_all();
}

void print_server_status(void) {
    printf("\n=== Server Status ===\n");
    printf("TCP Port: 23230, UDP Port: 23231\n");
    
    int connection_count = 0;
    if (connections) {
        connection_count = HASH_COUNT(connections);
    }
    printf("Active connections: %d\n", connection_count);
    
    int stream_count = 0;
    if (streams) {
        stream_count = HASH_COUNT(streams);
    }
    printf("Active streams: %d\n", stream_count);
    printf("Press Ctrl+C to stop server\n\n");
}

int main() {
    printf("Starting video conferencing server...\n");
    
    // Установка обработчиков сигналов
    setup_signal_handlers();
    
    // Инициализация серверов
    if (initialize_servers() < 0) {
        return 1;
    }
    
    print_server_status();
    
    struct epoll_event events[MAX_EVENTS];
    int timeout_ms = 1000; // 1 секунда таймаута для периодического статуса
    
    // Главный цикл epoll
    while (!stop_requested) {
        int n = epoll_wait(g_epoll_fd, events, MAX_EVENTS, timeout_ms);
        
        if (n < 0) {
            if (errno == EINTR) {
                continue; // Прервано сигналом
            }
            perror("epoll_wait failed");
            break;
        }
        
        // Обработка событий
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t events_mask = events[i].events;
            
            if (fd == g_tcp_fd) {
                // Новое TCP соединение
                handle_tcp_connection(g_epoll_fd, g_tcp_fd);
            } else if (fd == g_udp_fd) {
                // Входящий UDP пакет
                handle_udp_socket(g_udp_fd);
            } else {
                // Событие от клиентского соединения
                handle_client_event(g_epoll_fd, fd, events_mask);
            }
        }
        
        // Периодический вывод статуса (примерно раз в 10 секунд)
        static int status_counter = 0;
        if (n == 0) { // Таймаут
            status_counter++;
            if (status_counter >= 10) {
                print_server_status();
                status_counter = 0;
            }
        } else {
            status_counter = 0;
        }
        
        // Обработка отложенной записи для всех соединений
        Connection *conn, *tmp;
        HASH_ITER(hh, connections, conn, tmp) {
            if (buffer_get_data_size(&conn->write_buffer) > 0) {
                // Если есть данные для записи, добавляем EPOLLOUT если еще не добавлен
                epoll_modify(g_epoll_fd, conn->fd, EPOLLIN | EPOLLOUT | EPOLLET);
                handle_client_write(conn);
            }
        }
    }
    
    // Очистка ресурсов
    printf("Shutting down server...\n");
    cleanup_servers();
    
    printf("Server stopped gracefully\n");
    return 0;
}