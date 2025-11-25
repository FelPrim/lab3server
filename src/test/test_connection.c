#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "../connection.h"
#include "../stream.h"
#include "../call.h"
#include "../buffer.h"
#include "../test_common.h"
#include "../integrity_check.h"

static Connection* make_conn(int fd) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(9090);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    return connection_new(fd, &addr);
}

bool test_connection_new_delete() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_connection_new_delete");
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* c = make_conn(fd);

    TEST_ASSERT(&ctx, c != NULL, "Connection should be created");
    TEST_ASSERT(&ctx, c->fd == fd, "FD should match");
    TEST_ASSERT(&ctx, c->read_buffer.position == 0, "Read buffer should be initialized");
    TEST_ASSERT(&ctx, c->write_buffer.position == 0, "Write buffer should be initialized");
    TEST_ASSERT(&ctx, !c->udp_handshake_complete, "UDP handshake should not be complete initially");
    
    // Проверяем что соединение добавлено в глобальную таблицу
    Connection* found = connection_find(fd);
    TEST_ASSERT(&ctx, found == c, "Should find connection in global registry");
    
    // Проверяем что массивы инициализированы
    bool watch_streams_null = true;
    bool own_streams_null = true;
    bool calls_null = true;
    
    for (int i = 0; i < MAX_INPUT; i++) {
        if (c->watch_streams[i] != NULL) {
            watch_streams_null = false;
            break;
        }
    }
    
    for (int i = 0; i < MAX_OUTPUT; i++) {
        if (c->own_streams[i] != NULL) {
            own_streams_null = false;
            break;
        }
    }
    
    for (int i = 0; i < MAX_CONNECTION_CALLS; i++) {
        if (c->calls[i] != NULL) {
            calls_null = false;
            break;
        }
    }
    
    TEST_ASSERT(&ctx, watch_streams_null, "All watch_streams should be NULL initially");
    TEST_ASSERT(&ctx, own_streams_null, "All own_streams should be NULL initially");
    TEST_ASSERT(&ctx, calls_null, "All calls should be NULL initially");

    // Удаляем соединение
    connection_delete(c);
    
    // Проверяем что соединение удалено из глобальной таблицы
    found = connection_find(fd);
    TEST_ASSERT(&ctx, found == NULL, "Should not find connection after deletion");
bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");
    TEST_REPORT(&ctx, "test_connection_new_delete");
}

bool test_connection_stream_management() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_connection_stream_management");
    
    int f1 = socket(AF_INET, SOCK_STREAM, 0);
    int f2 = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner = make_conn(f1);
    Connection* viewer = make_conn(f2);

    int current_streams = 0;
    for (int i = 0; i < MAX_OUTPUT; i++) {
        if (owner->own_streams[i] != NULL) current_streams++;
    }
    TEST_ASSERT(&ctx, current_streams < MAX_OUTPUT, "Owner should have space for new streams");
    // Устанавливаем UDP для viewer (требуется для добавления получателя)
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(9091);
    inet_pton(AF_INET, "127.0.0.1", &udp_addr.sin_addr);
    connection_set_udp_addr(viewer, &udp_addr);
    connection_set_udp_handshake_complete(viewer);

    // Создаем стримы через высокоуровневый API
    Stream* s1 = stream_new(100, owner, NULL);
    Stream* s2 = stream_new(200, owner, NULL);
    TEST_ASSERT(&ctx, s1 != NULL, "Should create stream 1");
    TEST_ASSERT(&ctx, s2 != NULL, "Should create stream 2");

    // Проверяем что стримы добавлены к владельцу
    TEST_ASSERT(&ctx, connection_is_owning_stream(owner, s1), "Owner should own stream 1");
    TEST_ASSERT(&ctx, connection_is_owning_stream(owner, s2), "Owner should own stream 2");
    
    // Добавляем viewer как получателя
    TEST_ASSERT(&ctx, stream_add_recipient(s1, viewer) == 0, "Should add viewer to stream 1");
    TEST_ASSERT(&ctx, stream_add_recipient(s2, viewer) == 0, "Should add viewer to stream 2");
    
    // Проверяем что viewer наблюдает стримы
    TEST_ASSERT(&ctx, connection_is_watching_stream(viewer, s1), "Viewer should watch stream 1");
    TEST_ASSERT(&ctx, connection_is_watching_stream(viewer, s2), "Viewer should watch stream 2");
    
    // Поиск стрима по ID
    Stream* found1 = connection_find_stream_by_id(owner, 100);
    Stream* found2 = connection_find_stream_by_id(viewer, 200);
    TEST_ASSERT(&ctx, found1 == s1, "Should find stream 1 in owner");
    TEST_ASSERT(&ctx, found2 == s2, "Should find stream 2 in viewer");
    
    // Удаляем стримы - они автоматически удалятся из соединений
    stream_delete(s1);
    stream_delete(s2);
    
    // Проверяем что стримы удалены из соединений
    TEST_ASSERT(&ctx, !connection_is_owning_stream(owner, s1), "Owner should not own stream 1 after deletion");
    TEST_ASSERT(&ctx, !connection_is_watching_stream(viewer, s1), "Viewer should not watch stream 1 after deletion");

    // Очищаем соединения
    connection_delete(owner);
    connection_delete(viewer);

bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");

    TEST_REPORT(&ctx, "test_connection_stream_management");
}

bool test_connection_call_management() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_connection_call_management");
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* conn = make_conn(fd);

    // Создаем звонки через высокоуровневый API (предполагая, что call_new уже реализован)
    Call* call1 = call_new(300);
    Call* call2 = call_new(400);
    TEST_ASSERT(&ctx, call1 != NULL, "Should create call 1");
    TEST_ASSERT(&ctx, call2 != NULL, "Should create call 2");

    // Добавляем соединение в звонки
    TEST_ASSERT(&ctx, call_add_participant(call1, conn) == 0, "Should add connection to call 1");
    TEST_ASSERT(&ctx, call_add_participant(call2, conn) == 0, "Should add connection to call 2");

    TEST_ASSERT(&ctx, connection_is_in_call(conn, call1), "Connection should be in call 1");
    TEST_ASSERT(&ctx, connection_is_in_call(conn, call2), "Connection should be in call 2");
    
    // Поиск звонка по ID
    Call* found1 = connection_find_call_by_id(conn, 300);
    Call* found2 = connection_find_call_by_id(conn, 400);
    TEST_ASSERT(&ctx, found1 == call1, "Should find call 1 by ID");
    TEST_ASSERT(&ctx, found2 == call2, "Should find call 2 by ID");
    
    // Проверяем счетчик
    int count = connection_get_call_count(conn);
    TEST_ASSERT(&ctx, count == 2, "Should have 2 calls");
    
    // Удаляем соединение из звонков
    TEST_ASSERT(&ctx, call_remove_participant(call1, conn) == 0, "Should remove connection from call 1");
    TEST_ASSERT(&ctx, call_remove_participant(call2, conn) == 0, "Should remove connection from call 2");
    
    TEST_ASSERT(&ctx, !connection_is_in_call(conn, call1), "Connection should not be in call 1 after removal");
    TEST_ASSERT(&ctx, !connection_is_in_call(conn, call2), "Connection should not be in call 2 after removal");
    
    count = connection_get_call_count(conn);
    TEST_ASSERT(&ctx, count == 0, "Should have 0 calls after removal");

    // Очищаем в правильном порядке
    call_delete(call1);
    call_delete(call2);
    connection_delete(conn);

bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");
    
    TEST_REPORT(&ctx, "test_connection_call_management");
}

bool test_connection_udp_management() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_connection_udp_management");
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* c = make_conn(fd);

    TEST_ASSERT(&ctx, !connection_has_udp(c), "Should not have UDP initially");
    TEST_ASSERT(&ctx, !connection_is_udp_handshake_complete(c), "UDP handshake should not be complete initially");

    struct sockaddr_in udp;
    memset(&udp, 0, sizeof(udp));
    udp.sin_family = AF_INET;
    udp.sin_port   = htons(7777);
    inet_pton(AF_INET, "10.0.0.1", &udp.sin_addr);

    connection_set_udp_addr(c, &udp);
    TEST_ASSERT(&ctx, connection_has_udp(c), "Should have UDP after setting address");
    TEST_ASSERT(&ctx, c->udp_addr.sin_port == htons(7777), "UDP port should match");

    connection_set_udp_handshake_complete(c);
    TEST_ASSERT(&ctx, connection_is_udp_handshake_complete(c), "UDP handshake should be complete after setting");

    connection_delete(c);

bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");

    TEST_REPORT(&ctx, "test_connection_udp_management");
}

bool test_connection_delete_cleanup() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_connection_delete_cleanup");
    
    int f1 = socket(AF_INET, SOCK_STREAM, 0);
    int f2 = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner = make_conn(f1);
    Connection* viewer = make_conn(f2);

    // Устанавливаем UDP для viewer
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(9092);
    inet_pton(AF_INET, "127.0.0.1", &udp_addr.sin_addr);
    connection_set_udp_addr(viewer, &udp_addr);
    connection_set_udp_handshake_complete(viewer);

    // Создаем стрим и добавляем получателя
    Stream* stream = stream_new(500, owner, NULL);
    stream_add_recipient(stream, viewer);

    // Создаем звонок и добавляем оба соединения
    Call* call = call_new(600);
    call_add_participant(call, owner);
    call_add_participant(call, viewer);

    // Проверяем что связи установлены
    TEST_ASSERT(&ctx, connection_is_owning_stream(owner, stream), "Owner should own stream");
    TEST_ASSERT(&ctx, connection_is_watching_stream(viewer, stream), "Viewer should watch stream");
    TEST_ASSERT(&ctx, connection_is_in_call(owner, call), "Owner should be in call");
    TEST_ASSERT(&ctx, connection_is_in_call(viewer, call), "Viewer should be in call");

    // Удаляем owner - это должно очистить все связи
    connection_delete(owner);

    // Проверяем что стрим удален из viewer
    TEST_ASSERT(&ctx, !connection_is_watching_stream(viewer, stream), "Viewer should not watch stream after owner deletion");
    
    // Проверяем что owner удален из звонка
    // (здесь нужно проверить через call_get_participant_count, но пока оставим так)

    // Проверяем что owner удален из глобальной таблицы
    Connection* found = connection_find(f1);
    TEST_ASSERT(&ctx, found == NULL, "Should not find owner after deletion");

    // Очищаем оставшиеся ресурсы
    connection_delete(viewer);
    call_delete(call);


bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");

    TEST_REPORT(&ctx, "test_connection_delete_cleanup");
}

bool test_connection_close_all() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_connection_close_all");
    
    int f1 = socket(AF_INET, SOCK_STREAM, 0);
    int f2 = socket(AF_INET, SOCK_STREAM, 0);
    int f3 = socket(AF_INET, SOCK_STREAM, 0);
    
    Connection* c1 = make_conn(f1);
    Connection* c2 = make_conn(f2);
    Connection* c3 = make_conn(f3);

    // Проверяем что соединения добавлены в глобальную таблицу
    TEST_ASSERT(&ctx, connection_find(f1) == c1, "Should find connection 1");
    TEST_ASSERT(&ctx, connection_find(f2) == c2, "Should find connection 2");
    TEST_ASSERT(&ctx, connection_find(f3) == c3, "Should find connection 3");

    // Закрываем все соединения
    connection_close_all();

    // Проверяем что глобальная таблица пуста
    TEST_ASSERT(&ctx, connection_find(f1) == NULL, "Should not find connection 1 after close_all");
    TEST_ASSERT(&ctx, connection_find(f2) == NULL, "Should not find connection 2 after close_all");
    TEST_ASSERT(&ctx, connection_find(f3) == NULL, "Should not find connection 3 after close_all");
    TEST_ASSERT(&ctx, connections == NULL, "Global connections should be NULL after close_all");

bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");

    TEST_REPORT(&ctx, "test_connection_close_all");
}

bool run_all_connection_tests() {
    printf("Running connection tests...\n\n");
    
    bool all_passed = true;
    all_passed = test_connection_new_delete() && all_passed;
    all_passed = test_connection_stream_management() && all_passed;
    all_passed = test_connection_call_management() && all_passed;
    all_passed = test_connection_udp_management() && all_passed;
    all_passed = test_connection_delete_cleanup() && all_passed;
    all_passed = test_connection_close_all() && all_passed;
    
    if (all_passed) {
        printf("All connection tests passed! ✓\n\n");
    } else {
        printf("Some connection tests failed! ✗\n\n");
    }
    
    return all_passed;
}