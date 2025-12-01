#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <errno.h>
#include <time.h>

#include "network.h"
#include "connection.h"
#include "protocol.h"
#include "buffer_logic.h"
#include "integrity_check.h"

int g_epoll_fd = -1;
int g_tcp_fd = -1;
int g_udp_fd = -1;

volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) {
    printf("Received signal %d, shutting down...\n", sig);
    keep_running = 0;
}

void setup_signal_handlers(void) {
    // Используем signal вместо sigaction для простоты
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Игнорируем SIGPIPE чтобы не падать при записи в закрытый сокет
    signal(SIGPIPE, SIG_IGN);
}

int handle_tcp_accept(void) {
    struct sockaddr_in client_addr;
    int client_fd = accept_connection(g_tcp_fd, &client_addr);
    
    if (client_fd < 0) {
        if (client_fd != -2) { // -2 означает EAGAIN/EWOULDBLOCK
            perror("accept_connection failed");
        }
        return -1;
    }
    
    // Создаем и настраиваем соединение
    Connection* conn = connection_new(client_fd, &client_addr);
    if (!conn) {
        close(client_fd);
        return -1;
    }

    
    // Добавляем в epoll для чтения
    if (epoll_add(g_epoll_fd, client_fd, EPOLLIN | EPOLLET) != 0) {
        fprintf(stderr, "Failed to add client to epoll\n");
        connection_delete(conn);
        return -1;
    }
    
    // Отправляем handshake start
    send_server_handshake_start(conn);
    
    printf("New connection accepted: fd=%d, %s\n", 
           client_fd, connection_get_address_string(conn));
    
    return 0;
}

int handle_tcp_client(Connection* conn) {
    if (!conn) return -1;
    
    int result = connection_read_data(conn);
    
    if (result == 0) {
        // Соединение закрыто клиентом
        printf("Connection closed by client: %s\n", connection_get_address_string(conn));
        handle_connection_closed(conn);
        return 0;
    } else if (result < 0) {
        if (result != -2) { // -2 означает EAGAIN/EWOULDBLOCK
            // Ошибка чтения
            fprintf(stderr, "Read error from %s, closing connection\n", 
                    connection_get_address_string(conn));
            handle_connection_closed(conn);
            return -1;
        }
        // EAGAIN/EWOULDBLOCK - нормально для неблокирующего сокета
        return 0;
    }
    
    // Обрабатываем все полные сообщения в буфере
    Buffer* read_buf = &conn->read_buffer;
    
    while (read_buf->position > 0) {
        // Если expected_size еще не установлен, пытаемся установить
        if (read_buf->expected_size == 0) {
            if (buffer_protocol_set_expected(read_buf) != 0) {
                // Не можем определить размер сообщения - очищаем буфер
                fprintf(stderr, "Cannot determine message size, clearing buffer\n");
                buffer_clear(read_buf);
                break;
            }
        }
        
        // Проверяем, есть ли полное сообщение
        BufferResult state = buffer_protocol_state(read_buf);
        
        if (state == BUFFER_IS_COMPLETE) {
            // Извлекаем тип сообщения
            uint8_t message_type = read_buf->data[0];
            size_t payload_len = read_buf->expected_size - 1;
            const uint8_t* payload = read_buf->data + 1;
            
            // Обрабатываем сообщение
            handle_client_message(conn, message_type, payload, payload_len);
            
            // Удаляем обработанное сообщение из буфера
            buffer_protocol_consume(read_buf);
            
        } else if (state == BUFFER_OVERFLOW) {
            fprintf(stderr, "Buffer overflow, clearing\n");
            buffer_clear(read_buf);
            break;
        } else {
            // BUFFER_IS_INCOMPLETE - ждем больше данных
            break;
        }
    }
    
    return 0;
}

int handle_udp_data(void) {
    uint8_t buffer[UDP_PACKET_SIZE];
    struct sockaddr_in src_addr;
    
    int received = udp_receive_packet(g_udp_fd, buffer, sizeof(buffer), &src_addr);
    
    if (received > 0) {
        handle_udp_packet(buffer, (size_t)received, &src_addr);
    } else if (received < 0 && received != -2) {
        perror("udp_receive_packet failed");
    }
    
    return 0;
}

void cleanup(void) {
    printf("Cleaning up...\n");
    
    // Проверяем целостность перед завершением
    check_all_integrity();
    
    // Закрываем все соединения
    connection_close_all();
    
    // Закрываем серверные сокеты
    if (g_tcp_fd >= 0) {
        close(g_tcp_fd);
        g_tcp_fd = -1;
    }
    
    if (g_udp_fd >= 0) {
        close(g_udp_fd);
        g_udp_fd = -1;
    }
    
    if (g_epoll_fd >= 0) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }
    
    printf("Cleanup completed\n");
}

int main(int argc, char* argv[]) {
    printf("Starting Video Conference Server...\n");
    
    setup_signal_handlers();
    
    // Создаем epoll
    g_epoll_fd = create_epoll_fd();
    if (g_epoll_fd < 0) {
        fprintf(stderr, "Failed to create epoll\n");
        return 1;
    }
    
    // Создаем TCP сервер
    int tcp_port = 23230;
    if (argc > 1) {
        tcp_port = atoi(argv[1]);
    }
    
    g_tcp_fd = create_tcp_server(tcp_port);
    if (g_tcp_fd < 0) {
        fprintf(stderr, "Failed to create TCP server\n");
        cleanup();
        return 1;
    }
    
    // Создаем UDP сервер
    int udp_port = 23231;
    if (argc > 2) {
        udp_port = atoi(argv[2]);
    }
    
    g_udp_fd = create_udp_server(udp_port);
    if (g_udp_fd < 0) {
        fprintf(stderr, "Failed to create UDP server\n");
        cleanup();
        return 1;
    }
    
    // Добавляем серверные сокеты в epoll
    if (epoll_add(g_epoll_fd, g_tcp_fd, EPOLLIN) != 0) {
        fprintf(stderr, "Failed to add TCP server to epoll\n");
        cleanup();
        return 1;
    }
    
    if (epoll_add(g_epoll_fd, g_udp_fd, EPOLLIN) != 0) {
        fprintf(stderr, "Failed to add UDP server to epoll\n");
        cleanup();
        return 1;
    }
    
    printf("Server started successfully\n");
    printf("TCP port: %d, UDP port: %d\n", tcp_port, udp_port);
    printf("Press Ctrl+C to stop the server\n");
    
    // Главный цикл
    struct epoll_event events[100];
    
    while (keep_running) {
        int nfds = epoll_wait(g_epoll_fd, events, 100, 1000); // 1 second timeout
        
        if (nfds < 0) {
            if (errno == EINTR) {
                continue; // Сигнал прервал вызов
            }
            perror("epoll_wait failed");
            break;
        }
        
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            
            if (fd == g_tcp_fd) {
                // Новое TCP соединение
                handle_tcp_accept();
            } else if (fd == g_udp_fd) {
                // UDP данные
                handle_udp_data();
            } else {
                // TCP клиент
                Connection* conn = connection_find(fd);
                if (conn) {
                    if (events[i].events & EPOLLIN) {
                        handle_tcp_client(conn);
                    }
                    if (events[i].events & EPOLLOUT) {
                        connection_write_data(conn);
                    }
                } else {
                    // Соединение не найдено - удаляем из epoll
                    epoll_remove(g_epoll_fd, fd);
                    close(fd);
                }
            }
        }
        
        // Периодическая проверка целостности (каждые 60 секунд)
        static time_t last_check = 0;
        time_t now = time(NULL);
        if (now - last_check >= 60) {
            check_all_integrity();
            last_check = now;
        }
    }
    
    cleanup();
    printf("Server stopped\n");
    
    return 0;
}