#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "connection.h"
#include "connection_logic.h"
#include "stream.h"
#include "stream_logic.h"
#include "buffer.h"

static Connection* make_conn(int fd) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(9090);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    return connection_create(fd, &addr);
}

/* ---------------------------------------------------------
 * TEST 1 — базовое создание
 * --------------------------------------------------------- */
void test_connection_basic_create() {
    printf("=== test_connection_basic_create ===\n");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* c = make_conn(fd);

    assert(c != NULL);
    assert(c->fd == fd);

    assert(c->read_buffer.position == 0);
    assert(c->write_buffer.position == 0);

    for (int i = 0; i < MAX_INPUT; i++)
        assert(c->watch_streams[i] == NULL);

    for (int i = 0; i < MAX_OUTPUT; i++)
        assert(c->own_streams[i] == NULL);

    connection_destroy_full(c);

    printf("✓ passed\n\n");
}

/* ---------------------------------------------------------
 * TEST 2 — глобальная таблица  
 * --------------------------------------------------------- */
void test_connection_table() {
    printf("=== test_connection_table ===\n");

    int fd1 = socket(AF_INET, SOCK_STREAM, 0);
    int fd2 = socket(AF_INET, SOCK_STREAM, 0);

    Connection* a = make_conn(fd1);
    Connection* b = make_conn(fd2);

    connection_add(a);
    connection_add(b);

    assert(connection_find(fd1) == a);
    assert(connection_find(fd2) == b);
    assert(connection_find(9999) == NULL);

    connection_remove(fd1);
    assert(connection_find(fd1) == NULL);

    connection_destroy_full(b);

    printf("✓ passed\n\n");
}

/* ---------------------------------------------------------
 * TEST 3 — UDP
 * --------------------------------------------------------- */
void test_connection_udp() {
    printf("=== test_connection_udp ===\n");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* c = make_conn(fd);

    assert(!connection_has_udp(c));

    struct sockaddr_in udp;
    memset(&udp, 0, sizeof(udp));
    udp.sin_family = AF_INET;
    udp.sin_port   = htons(7777);
    inet_pton(AF_INET, "10.0.0.1", &udp.sin_addr);

    connection_set_udp_addr(c, &udp);

    assert(connection_has_udp(c));
    assert(c->udp_addr.sin_port == htons(7777));

    connection_destroy_full(c);

    printf("✓ passed\n\n");
}

/* ---------------------------------------------------------
 * TEST 4 — stream management
 * --------------------------------------------------------- */
void test_connection_streams() {
    printf("=== test_connection_streams ===\n");

    int f1 = socket(AF_INET, SOCK_STREAM, 0);
    int f2 = socket(AF_INET, SOCK_STREAM, 0);

    Connection* owner  = make_conn(f1);
    Connection* viewer = make_conn(f2);

    // stream_create автоматически добавляет стрим к владельцу
    Stream* s1 = stream_create(100, owner);
    Stream* s2 = stream_create(200, owner);

    // Проверяем, что стримы автоматически добавились к владельцу
    assert(connection_get_own_stream_count(owner) == 2);
    assert(connection_is_owning_stream(owner, s1));
    assert(connection_is_owning_stream(owner, s2));

    // Добавляем стримы к просматривающему (это не делается автоматически)
    assert(connection_add_watch_stream(viewer, s1) == 0);
    assert(connection_add_watch_stream(viewer, s2) == 0);

    assert(connection_get_watch_stream_count(viewer) == 2);
    assert(connection_get_own_stream_count(owner) == 2);

    // Отписываем viewer от стримов
    connection_detach_from_streams(viewer);

    assert(connection_get_watch_stream_count(viewer) == 0);

    // Удаляем owner (должен удалить и стримы)
    connection_destroy_full(owner);

    // Удаляем viewer
    connection_destroy_full(viewer);

    printf("✓ passed\n\n");
}

/* ---------------------------------------------------------
 * TEST 5 — limits
 * --------------------------------------------------------- */
void test_connection_limits() {
    printf("=== test_connection_limits ===\n");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* c = make_conn(fd);

    // Заполняем все слоты для owned streams через stream_create
    Stream* streams[MAX_OUTPUT];
    for (int i = 0; i < MAX_OUTPUT; i++) {
        streams[i] = stream_create(300 + i, c);
        assert(streams[i] != NULL); // Убеждаемся, что создание прошло успешно
        assert(connection_get_own_stream_count(c) == i + 1);
    }

    // Попытка создать еще один стрим должна вернуть NULL (лимит исчерпан)
    Stream* overflow = stream_create(999, c);
    assert(overflow == NULL);

    // cleanup - connection_destroy_full удалит все стримы
    connection_destroy_full(c);

    printf("✓ passed\n\n");
}

/* ---------------------------------------------------------
 * TEST 6 — address string
 * --------------------------------------------------------- */
void test_connection_addr_string() {
    printf("=== test_connection_addr_string ===\n");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* c = make_conn(fd);

    const char* addr_str = connection_get_address_string(c);
    assert(addr_str != NULL);
    assert(strstr(addr_str, "127.0.0.1") != NULL);
    assert(strstr(addr_str, "9090") != NULL);

    connection_destroy_full(c);

    printf("✓ passed\n\n");
}

/* ---------------------------------------------------------
 * MAIN
 * --------------------------------------------------------- */
void run_all_connection_tests() {
    printf("Running connection tests...\n\n");

    test_connection_basic_create();
    test_connection_table();
    test_connection_udp();
    test_connection_streams();
    test_connection_limits();
    test_connection_addr_string();

    printf("All connection tests passed! ✓\n\n");
}