#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "../call.h"
#include "../connection.h"
#include "../stream.h"
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

bool test_call_new_delete() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_call_new_delete");
    
    Call* call = call_new(100);
    TEST_ASSERT(&ctx, call != NULL, "Call should be created");
    TEST_ASSERT(&ctx, call->call_id == 100, "Call ID should match");
    
    // Проверяем что массивы инициализированы
    bool participants_null = true;
    bool streams_null = true;
    
    for (int i = 0; i < MAX_CALL_PARTICIPANTS; i++) {
        if (call->participants[i] != NULL) {
            participants_null = false;
            break;
        }
    }
    
    for (int i = 0; i < MAX_CALL_STREAMS; i++) {
        if (call->streams[i] != NULL) {
            streams_null = false;
            break;
        }
    }
    
    TEST_ASSERT(&ctx, participants_null, "All participants should be NULL initially");
    TEST_ASSERT(&ctx, streams_null, "All streams should be NULL initially");
    
    // Проверяем что call добавлен в глобальную таблицу
    Call* found = call_find_by_id(100);
    TEST_ASSERT(&ctx, found == call, "Should find call in global registry");
    
    call_delete(call);
    
    // Проверяем что call удален из глобальной таблицы
    found = call_find_by_id(100);
    TEST_ASSERT(&ctx, found == NULL, "Should not find call after deletion");
    bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");

    TEST_REPORT(&ctx, "test_call_new_delete");
}

bool test_call_participant_management() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_call_participant_management");
    
    Call* call = call_new(200);
    int fd1 = socket(AF_INET, SOCK_STREAM, 0);
    int fd2 = socket(AF_INET, SOCK_STREAM, 0);
    Connection* conn1 = make_conn(fd1);
    Connection* conn2 = make_conn(fd2);

    // Добавляем участников через высокоуровневый API
    TEST_ASSERT(&ctx, call_add_participant(call, conn1) == 0, "Should add participant 1");
    TEST_ASSERT(&ctx, call_add_participant(call, conn2) == 0, "Should add participant 2");
    
    TEST_ASSERT(&ctx, call_has_participant(call, conn1), "Call should have participant 1");
    TEST_ASSERT(&ctx, call_has_participant(call, conn2), "Call should have participant 2");
    
    // Проверяем что звонки добавлены к соединениям
    TEST_ASSERT(&ctx, connection_is_in_call(conn1, call), "Connection 1 should be in call");
    TEST_ASSERT(&ctx, connection_is_in_call(conn2, call), "Connection 2 should be in call");
    
    // Проверяем счетчик
    int count = call_get_participant_count(call);
    TEST_ASSERT(&ctx, count == 2, "Should have 2 participants");
    
    // Поиск по ID
    Connection* found = call_find_participant_by_id(call, fd1);
    TEST_ASSERT(&ctx, found == conn1, "Should find participant by ID");
    
    // Удаляем участника
    TEST_ASSERT(&ctx, call_remove_participant(call, conn1) == 0, "Should remove participant 1");
    TEST_ASSERT(&ctx, !call_has_participant(call, conn1), "Call should not have participant 1");
    TEST_ASSERT(&ctx, !connection_is_in_call(conn1, call), "Connection 1 should not be in call after removal");
    
    count = call_get_participant_count(call);
    TEST_ASSERT(&ctx, count == 1, "Should have 1 participant after removal");

    call_delete(call);
    connection_delete(conn1);
    connection_delete(conn2);

    bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");
    TEST_REPORT(&ctx, "test_call_participant_management");
}

bool test_call_stream_management() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_call_stream_management");
    
    Call* call = call_new(300);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner = make_conn(fd);

    // Добавляем owner в call чтобы можно было создать приватный стрим
    call_add_participant(call, owner);

    // Создаем приватный стрим
    Stream* stream = stream_new(400, owner, call);
    TEST_ASSERT(&ctx, stream != NULL, "Should create private stream");

    // Проверяем что стрим добавлен в call
    TEST_ASSERT(&ctx, call_has_stream(call, stream), "Call should have stream");
    TEST_ASSERT(&ctx, stream_is_private(stream), "Stream should be private");
    TEST_ASSERT(&ctx, stream_get_call(stream) == call, "Stream should reference call");
    
    // Проверяем счетчик
    int count = call_get_stream_count(call);
    TEST_ASSERT(&ctx, count == 1, "Should have 1 stream");
    
    // Поиск по ID
    Stream* found = call_find_stream_by_id(call, 400);
    TEST_ASSERT(&ctx, found == stream, "Should find stream by ID");
    
    // Удаляем стрим из call
    TEST_ASSERT(&ctx, call_remove_stream(call, stream) == 0, "Should remove stream from call");
    TEST_ASSERT(&ctx, !call_has_stream(call, stream), "Call should not have stream");
    TEST_ASSERT(&ctx, !stream_is_private(stream), "Stream should not be private after removal");
    TEST_ASSERT(&ctx, stream_get_call(stream) == NULL, "Stream should not reference call after removal");
    
    count = call_get_stream_count(call);
    TEST_ASSERT(&ctx, count == 0, "Should have 0 streams after removal");

    // Удаляем стрим
    stream_delete(stream);
    call_delete(call);
    connection_delete(owner);

bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");

    TEST_REPORT(&ctx, "test_call_stream_management");
}

bool test_call_find_functions() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_call_find_functions");
    
    Call* call1 = call_new(500);
    Call* call2 = call_new(600);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* conn = make_conn(fd);

    // Добавляем соединение в оба звонка
    call_add_participant(call1, conn);
    call_add_participant(call2, conn);

    // Тестируем поиск по ID
    Call* found1 = call_find_by_id(500);
    Call* found2 = call_find_by_id(600);
    TEST_ASSERT(&ctx, found1 == call1, "Should find call 1 by ID");
    TEST_ASSERT(&ctx, found2 == call2, "Should find call 2 by ID");

    // Тестируем поиск по участнику
    Call* participant_results[2];
    int count = call_find_by_participant(conn, participant_results, 2);
    TEST_ASSERT(&ctx, count == 2, "Should find 2 calls for participant");
    
    bool found_call1 = false, found_call2 = false;
    for (int i = 0; i < count; i++) {
        if (participant_results[i] == call1) found_call1 = true;
        if (participant_results[i] == call2) found_call2 = true;
    }
    TEST_ASSERT(&ctx, found_call1 && found_call2, "Should find both calls for participant");

    // Очищаем
    call_delete(call1);
    call_delete(call2);
    connection_delete(conn);

bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");

    TEST_REPORT(&ctx, "test_call_find_functions");
}

bool test_call_delete_cleanup() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_call_delete_cleanup");
    
    Call* call = call_new(700);
    int fd1 = socket(AF_INET, SOCK_STREAM, 0);
    int fd2 = socket(AF_INET, SOCK_STREAM, 0);
    Connection* conn1 = make_conn(fd1);
    Connection* conn2 = make_conn(fd2);

    // Добавляем участников
    call_add_participant(call, conn1);
    call_add_participant(call, conn2);

    // Создаем приватные стримы
    Stream* stream1 = stream_new(800, conn1, call);
    Stream* stream2 = stream_new(900, conn2, call);

    // Проверяем что связи установлены
    TEST_ASSERT(&ctx, call_has_participant(call, conn1), "Call should have participant 1");
    TEST_ASSERT(&ctx, call_has_participant(call, conn2), "Call should have participant 2");
    TEST_ASSERT(&ctx, call_has_stream(call, stream1), "Call should have stream 1");
    TEST_ASSERT(&ctx, call_has_stream(call, stream2), "Call should have stream 2");
    TEST_ASSERT(&ctx, connection_is_in_call(conn1, call), "Connection 1 should be in call");
    TEST_ASSERT(&ctx, connection_is_in_call(conn2, call), "Connection 2 should be in call");

    // Удаляем call - это должно очистить все связи
    call_delete(call);

    // Проверяем что участники удалены из call
    TEST_ASSERT(&ctx, !connection_is_in_call(conn1, call), "Connection 1 should not be in call after deletion");
    TEST_ASSERT(&ctx, !connection_is_in_call(conn2, call), "Connection 2 should not be in call after deletion");

    // Проверяем что call удален из глобальной таблицы
    Call* found = call_find_by_id(700);
    TEST_ASSERT(&ctx, found == NULL, "Should not find call after deletion");

    // Очищаем оставшиеся ресурсы
    connection_delete(conn1);
    connection_delete(conn2);

bool integrity_ok = check_all_integrity();
TEST_ASSERT(&ctx, integrity_ok, "System integrity check failed after test");

    TEST_REPORT(&ctx, "test_call_delete_cleanup");
}

bool run_all_call_tests() {
    printf("Running call tests...\n\n");
    
    bool all_passed = true;
    all_passed = test_call_new_delete() && all_passed;
    all_passed = test_call_participant_management() && all_passed;
    all_passed = test_call_stream_management() && all_passed;
    all_passed = test_call_find_functions() && all_passed;
    all_passed = test_call_delete_cleanup() && all_passed;
    
    if (all_passed) {
        printf("All call tests passed! ✓\n\n");
    } else {
        printf("Some call tests failed! ✗\n\n");
    }
    
    return all_passed;
}