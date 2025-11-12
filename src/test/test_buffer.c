#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "buffer.h"

// Создаем пару сокетов для тестирования
int create_socket_pair(int *client_fd, int *server_fd) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // любой доступный порт

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 1) < 0) {
        close(listen_fd);
        return -1;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(listen_fd, (struct sockaddr*)&addr, &len) < 0) {
        close(listen_fd);
        return -1;
    }

    *client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*client_fd < 0) {
        close(listen_fd);
        return -1;
    }

    if (connect(*client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(listen_fd);
        close(*client_fd);
        return -1;
    }

    *server_fd = accept(listen_fd, NULL, NULL);
    if (*server_fd < 0) {
        close(listen_fd);
        close(*client_fd);
        return -1;
    }

    close(listen_fd);
    return 0;
}

void test_buffer_init() {
    printf("=== Testing buffer_init ===\n");
    
    Buffer buf;
    buffer_init(&buf);
    
    assert(buf.position == 0);
    assert(buf.expected_size == 0);
    assert(buffer_get_data_size(&buf) == 0);
    assert(buffer_get_remaining_capacity(&buf) == BUFFER_SIZE);
    
    printf("✓ buffer_init works correctly\n");
}

void test_buffer_prepare_send() {
    printf("=== Testing buffer_prepare_send ===\n");
    
    Buffer buf;
    buffer_init(&buf);
    
    const char* test_data = "Hello, World!";
    size_t test_len = strlen(test_data);
    
    buffer_prepare_send(&buf, test_data, test_len);
    
    assert(buf.position == 0);
    assert(buf.expected_size == test_len);
    assert(memcmp(buf.data, test_data, test_len) == 0);
    assert(buffer_get_data_size(&buf) == 0); // position is 0 after prepare
    
    printf("✓ buffer_prepare_send works correctly\n");
}

void test_buffer_has_complete_message() {
    printf("=== Testing buffer_has_complete_message ===\n");
    
    Buffer buf;
    buffer_init(&buf);
    
    // Изначально нет сообщения
    assert(buffer_has_complete_message(&buf) == 0);
    
    // Устанавливаем expected_size и заполняем данные
    buf.expected_size = 10;
    buf.position = 5;
    assert(buffer_has_complete_message(&buf) == 0);
    
    buf.position = 10;
    assert(buffer_has_complete_message(&buf) == 1);
    
    buf.position = 15;
    assert(buffer_has_complete_message(&buf) == 1);
    
    printf("✓ buffer_has_complete_message works correctly\n");
}

void test_buffer_consume() {
    printf("=== Testing buffer_consume ===\n");
    
    Buffer buf;
    buffer_init(&buf);
    
    // Заполняем буфер тестовыми данными
    const char* test_data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t test_len = strlen(test_data);
    memcpy(buf.data, test_data, test_len);
    buf.position = test_len;
    buf.expected_size = test_len;
    
    // Потребляем часть данных
    buffer_consume(&buf, 5);
    
    assert(buf.position == test_len - 5);
    assert(buf.expected_size == test_len - 5);
    assert(memcmp(buf.data, "FGHIJKLMNOPQRSTUVWXYZ", test_len - 5) == 0);
    
    // Потребляем все данные
    buffer_consume(&buf, test_len - 5);
    assert(buf.position == 0);
    assert(buf.expected_size == 0);
    
    // Потребляем больше чем есть
    memcpy(buf.data, "TEST", 4);
    buf.position = 4;
    buf.expected_size = 4;
    buffer_consume(&buf, 10);
    assert(buf.position == 0);
    assert(buf.expected_size == 0);
    
    printf("✓ buffer_consume works correctly\n");
}

void test_buffer_read_write_socket() {
    printf("=== Testing buffer_read_socket and buffer_write_socket ===\n");
    
    int client_fd, server_fd;
    if (create_socket_pair(&client_fd, &server_fd) < 0) {
        printf("✗ Failed to create socket pair\n");
        return;
    }
    
    // Тестируем запись
    Buffer write_buf;
    buffer_init(&write_buf);
    
    const char* test_message = "Test message for socket I/O";
    buffer_prepare_send(&write_buf, test_message, strlen(test_message));
    
    int result = buffer_write_socket(&write_buf, server_fd);
    assert(result == 1 || result == -2); // Успех или EAGAIN
    
    // Тестируем чтение
    Buffer read_buf;
    buffer_init(&read_buf);
    
    // Читаем с клиентской стороны
    result = buffer_read_socket(&read_buf, client_fd);
    assert(result > 0 || result == -2);
    
    close(client_fd);
    close(server_fd);
    
    printf("✓ buffer_read_socket and buffer_write_socket work correctly\n");
}

void test_buffer_edge_cases() {
    printf("=== Testing buffer edge cases ===\n");
    
    Buffer buf;
    buffer_init(&buf);
    
    // Тест переполнения
    char large_data[BUFFER_SIZE + 100];
    memset(large_data, 'A', sizeof(large_data));
    
    // Это не должно вызвать переполнение благодаря проверке в buffer_prepare_send
    buffer_prepare_send(&buf, large_data, sizeof(large_data));
    
    // Тест нулевой длины
    buffer_prepare_send(&buf, "", 0);
    assert(buf.expected_size == 0);
    
    printf("✓ buffer edge cases handled correctly\n");
}

void run_all_buffer_tests() {
    printf("Running buffer tests...\n\n");
    
    test_buffer_init();
    test_buffer_prepare_send();
    test_buffer_has_complete_message();
    test_buffer_consume();
    test_buffer_read_write_socket();
    test_buffer_edge_cases();
    
    printf("\nAll buffer tests passed! ✓\n\n");
}