#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "../connection.h"
#include "../stream.h"
#include "../call.h"
#include "../stream.h"
#include "../call.h"
#include "../integrity_check.h"
#include "../test_common.h"

static Connection* make_conn(int fd) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9090);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    Connection* conn = connection_new(fd, &addr);
    return conn;
}

bool test_integrity_basic_connections() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_integrity_basic_connections");
    
    cleanup_globals();
    
    // Создаем несколько соединений без связей
    Connection* conn1 = make_conn(3);
    Connection* conn2 = make_conn(4);
    
    TEST_ASSERT(&ctx, conn1 != NULL, "Should create connection 1");
    TEST_ASSERT(&ctx, conn2 != NULL, "Should create connection 2");
    
    // Проверяем целостность системы с пустыми связями
    bool integrity_ok = check_all_integrity();
    TEST_ASSERT(&ctx, integrity_ok, "Integrity check should pass with basic connections");
    
    
    TEST_REPORT(&ctx, "test_integrity_basic_connections");
}

bool test_integrity_stream_ownership() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_integrity_stream_ownership");
    
    cleanup_globals();
    
    // Создаем соединение и стрим
    Connection* owner = make_conn(3);
    TEST_ASSERT(&ctx, owner != NULL, "Should create owner connection");
    
    Stream* stream = stream_new(100, owner, NULL);
    TEST_ASSERT(&ctx, stream != NULL, "Should create stream");
    
    // Проверяем целостность системы
    bool integrity_ok = check_all_integrity();
    TEST_ASSERT(&ctx, integrity_ok, "Integrity check should pass with stream ownership");
    
    // Очищаем
    stream_delete(stream);
    
    
    TEST_REPORT(&ctx, "test_integrity_stream_ownership");
}

bool test_integrity_stream_recipients() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_integrity_stream_recipients");
    
    cleanup_globals();
    
    // Создаем соединения
    Connection* owner = make_conn(3);
    Connection* viewer = make_conn(4);
    
    TEST_ASSERT(&ctx, owner != NULL, "Should create owner connection");
    TEST_ASSERT(&ctx, viewer != NULL, "Should create viewer connection");
    
    // Создаем стрим и добавляем получателя
    Stream* stream = stream_new(200, owner, NULL);
    TEST_ASSERT(&ctx, stream != NULL, "Should create stream");
    
    int result = stream_add_recipient(stream, viewer);
    TEST_ASSERT(&ctx, result == 0, "Should add recipient to stream");
    
    // Проверяем целостность
    bool integrity_ok = check_all_integrity();
    TEST_ASSERT(&ctx, integrity_ok, "Integrity check should pass with stream recipients");
    
    // Очищаем
    stream_delete(stream);
    
    TEST_REPORT(&ctx, "test_integrity_stream_recipients");
}

bool test_integrity_call_participants() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_integrity_call_participants");
    
    cleanup_globals();
    
    // Создаем соединения
    Connection* participant1 = make_conn(3);
    Connection* participant2 = make_conn(4);
    Connection* participant3 = make_conn(5);
    
    // Создаем звонки
    Call* call1 = call_new(300);
    Call* call2 = call_new(400);
    
    TEST_ASSERT(&ctx, call1 != NULL, "Should create call 1");
    TEST_ASSERT(&ctx, call2 != NULL, "Should create call 2");
    
    // Добавляем участников в звонки
    TEST_ASSERT(&ctx, call_add_participant(call1, participant1) == 0, 
                "Should add participant1 to call1");
    TEST_ASSERT(&ctx, call_add_participant(call2, participant2) == 0, 
                "Should add participant2 to call2");
    TEST_ASSERT(&ctx, call_add_participant(call2, participant3) == 0,
                "Should add participant3 to call2");
    
    // Создаем приватный стрим
    Stream* private_stream = stream_new(500, participant2, call2);
    TEST_ASSERT(&ctx, private_stream != NULL, "Should create private stream");
    
    // Проверяем целостность
    bool integrity_ok = check_all_integrity();
    TEST_ASSERT(&ctx, integrity_ok, "Integrity check should pass with call participants");
    
    // Очищаем
    stream_delete(private_stream);
    call_delete(call1);
    call_delete(call2);
    
    TEST_REPORT(&ctx, "test_integrity_call_participants");
}

bool test_integrity_complex_scenario() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_integrity_complex_scenario");
    
    cleanup_globals();
    
    // Создаем соединения
    Connection* owner1 = make_conn(3);
    Connection* owner2 = make_conn(4);
    Connection* viewer1 = make_conn(5);
    
    // Создаем звонки
    Call* call1 = call_new(600);
    Call* call2 = call_new(700);
    
    // Добавляем участников
    TEST_ASSERT(&ctx, call_add_participant(call1, owner1) == 0, 
                "Should add owner1 to call1");
    TEST_ASSERT(&ctx, call_add_participant(call2, owner2) == 0, 
                "Should add owner2 to call2");
    TEST_ASSERT(&ctx, call_add_participant(call2, viewer1) == 0,
                "Should add viewer1 to call2");
    
    // Создаем приватные стримы
    Stream* stream1 = stream_new(800, owner1, call1);
    Stream* stream2 = stream_new(900, owner2, call2);
    
    TEST_ASSERT(&ctx, stream1 != NULL, "Should create stream1");
    TEST_ASSERT(&ctx, stream2 != NULL, "Should create stream2");
    
    // Добавляем получателей
    TEST_ASSERT(&ctx, stream_add_recipient(stream1, viewer1) == 0, 
                "Should add viewer1 to stream1");
    TEST_ASSERT(&ctx, stream_add_recipient(stream2, owner1) == 0, 
                "Should add owner1 to stream2");
    
    // Проверяем целостность
    bool integrity_ok = check_all_integrity();
    TEST_ASSERT(&ctx, integrity_ok, "Integrity check should pass with complex scenario");
    
    // Очищаем
    stream_delete(stream1);
    stream_delete(stream2);
    call_delete(call1);
    call_delete(call2);
    
    TEST_REPORT(&ctx, "test_integrity_complex_scenario");
}

bool run_all_integrity_tests() {
    printf("Running integrity tests...\n\n");
    
    bool all_passed = true;
    all_passed = test_integrity_basic_connections() && all_passed;
    all_passed = test_integrity_stream_ownership() && all_passed;
    all_passed = test_integrity_stream_recipients() && all_passed;
    all_passed = test_integrity_call_participants() && all_passed;
    all_passed = test_integrity_complex_scenario() && all_passed;
    
    if (all_passed) {
        printf("All integrity tests passed! ✓\n\n");
    } else {
        printf("Some integrity tests failed! ✗\n\n");
    }
    
    return all_passed;
}