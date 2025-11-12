#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "network.h"
#include <fcntl.h>

void test_set_nonblocking() {
    printf("=== Testing set_nonblocking ===\n");
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);
    
    // Устанавливаем неблокирующий режим
    int result = set_nonblocking(sockfd);
    assert(result == 0);
    
    // Проверяем, что сокет действительно неблокирующий
    int flags = fcntl(sockfd, F_GETFL, 0);
    assert(flags != -1);
    assert(flags & O_NONBLOCK);
    
    close(sockfd);
    printf("✓ set_nonblocking works correctly\n");
}

void test_create_tcp_server() {
    printf("=== Testing create_tcp_server ===\n");
    
    int port = 23232; // Используем тестовый порт
    int server_fd = create_tcp_server(port);
    assert(server_fd >= 0);
    
    // Проверяем, что сокет слушает
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int result = getsockname(server_fd, (struct sockaddr*)&addr, &addr_len);
    assert(result == 0);
    assert(ntohs(addr.sin_port) == port);
    
    close(server_fd);
    printf("✓ create_tcp_server works correctly\n");
}

void test_create_udp_server() {
    printf("=== Testing create_udp_server ===\n");
    
    int port = 23233; // Используем тестовый порт
    int server_fd = create_udp_server(port);
    assert(server_fd >= 0);
    
    // Проверяем, что сокет привязан к правильному порту
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int result = getsockname(server_fd, (struct sockaddr*)&addr, &addr_len);
    assert(result == 0);
    assert(ntohs(addr.sin_port) == port);
    
    close(server_fd);
    printf("✓ create_udp_server works correctly\n");
}

void test_epoll_functions() {
    printf("=== Testing epoll functions ===\n");
    
    int epoll_fd = create_epoll_fd();
    assert(epoll_fd >= 0);
    
    // Создаем тестовый сокет
    int test_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(test_fd >= 0);
    
    // Добавляем в epoll
    int result = epoll_add(epoll_fd, test_fd, EPOLLIN);
    assert(result == 0);
    
    // Модифицируем события
    result = epoll_modify(epoll_fd, test_fd, EPOLLIN | EPOLLOUT);
    assert(result == 0);
    
    // Удаляем из epoll
    result = epoll_remove(epoll_fd, test_fd);
    assert(result == 0);
    
    close(test_fd);
    close(epoll_fd);
    printf("✓ epoll functions work correctly\n");
}

void test_accept_connection() {
    printf("=== Testing accept_connection ===\n");
    
    // Создаем TCP сервер
    int server_fd = create_tcp_server(23234);
    assert(server_fd >= 0);
    
    // Создаем клиентский сокет и подключаемся
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(client_fd >= 0);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(23234);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    // Подключаемся в отдельном потоке или с таймаутом
    connect(client_fd, (struct sockaddr*)&addr, sizeof(addr));
    
    // Принимаем соединение
    struct sockaddr_in client_addr;
    int accepted_fd = accept_connection(server_fd, &client_addr);
    assert(accepted_fd >= 0 || accepted_fd == -2); // Успех или EAGAIN
    
    if (accepted_fd > 0) {
        close(accepted_fd);
    }
    
    close(client_fd);
    close(server_fd);
    printf("✓ accept_connection works correctly\n");
}

void test_udp_send_receive() {
    printf("=== Testing UDP send/receive ===\n");
    
    int server_fd = create_udp_server(23235);
    assert(server_fd >= 0);
    
    int client_fd = create_udp_server(23236); // Клиент тоже UDP
    assert(client_fd >= 0);
    
    // Подготавливаем тестовые данные
    const char* test_data = "UDP test message";
    size_t test_len = strlen(test_data);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(23235);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    
    // Отправляем пакет
    int send_result = udp_send_packet(client_fd, test_data, test_len, &server_addr);
    assert(send_result == (int)test_len || send_result == -2);
    
    // Принимаем пакет
    char buffer[1024];
    struct sockaddr_in src_addr;
    int recv_result = udp_receive_packet(server_fd, buffer, sizeof(buffer), &src_addr);
    assert(recv_result == (int)test_len || recv_result == -2);
    
    if (recv_result > 0) {
        assert(memcmp(buffer, test_data, test_len) == 0);
    }
    
    close(server_fd);
    close(client_fd);
    printf("✓ UDP send/receive works correctly\n");
}

void test_sockaddr_utils() {
    printf("=== Testing sockaddr utilities ===\n");
    
    struct sockaddr_in addr1, addr2;
    memset(&addr1, 0, sizeof(addr1));
    memset(&addr2, 0, sizeof(addr2));
    
    addr1.sin_family = AF_INET;
    addr1.sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &addr1.sin_addr);
    
    addr2.sin_family = AF_INET;
    addr2.sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &addr2.sin_addr);
    
    // Тестируем сравнение
    int cmp_result = compare_sockaddr(&addr1, &addr2);
    assert(cmp_result == 0);
    
    // Меняем порт и проверяем, что сравнение обнаруживает разницу
    addr2.sin_port = htons(8081);
    cmp_result = compare_sockaddr(&addr1, &addr2);
    assert(cmp_result == 1);
    
    // Тестируем преобразование в строку
    char addr_str[64];
    sockaddr_to_string(&addr1, addr_str, sizeof(addr_str));
    assert(strlen(addr_str) > 0);
    
    printf("✓ sockaddr utilities work correctly\n");
}

void test_async_io() {
    printf("=== Testing async I/O ===\n");
    
    // Создаем пару сокетов для тестирования
    int fds[2];
    int result = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(result == 0);
    
    // Устанавливаем неблокирующий режим
    set_nonblocking(fds[0]);
    set_nonblocking(fds[1]);
    
    const char* test_data = "Async I/O test";
    size_t test_len = strlen(test_data);
    
    // Тестируем асинхронную запись
    int write_result = async_write(fds[0], test_data, test_len);
    assert(write_result == (int)test_len || write_result == -2);
    
    // Тестируем асинхронное чтение
    char buffer[1024];
    int read_result = async_read(fds[1], buffer, sizeof(buffer));
    assert(read_result == (int)test_len || read_result == -2);
    
    if (read_result > 0) {
        assert(memcmp(buffer, test_data, test_len) == 0);
    }
    
    close(fds[0]);
    close(fds[1]);
    printf("✓ async I/O works correctly\n");
}

void run_all_network_tests() {
    printf("Running network tests...\n\n");
    
    test_set_nonblocking();
    test_create_tcp_server();
    test_create_udp_server();
    test_epoll_functions();
    test_accept_connection();
    test_udp_send_receive();
    test_sockaddr_utils();
    test_async_io();
    
    printf("\nAll network tests passed! ✓\n\n");
}
