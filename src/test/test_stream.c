#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "../stream.h"
#include "../connection.h"
#include "../call.h"
#include "../test_common.h"
#include "../integrity_check.h"

static Connection* make_conn(int fd) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(9090);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    Connection* conn = connection_new(fd, &addr);
    return conn;
}

bool test_stream_new_delete() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_stream_new_delete");
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner = make_conn(fd);

    // Тестируем базовое создание и уничтожение
    Stream* stream = stream_new(100, owner, NULL);
    TEST_ASSERT(&ctx, stream != NULL, "Stream should be created");
    TEST_ASSERT(&ctx, stream->stream_id == 100, "Stream ID should match");
    TEST_ASSERT(&ctx, stream->owner == owner, "Stream owner should match");
    TEST_ASSERT(&ctx, stream->call == NULL, "Stream call should be NULL");
    
    // Проверяем что массивы инициализированы
    int count = stream_get_recipient_count(stream);
    TEST_ASSERT(&ctx, count == 0, "Should have 0 recipients initially");
    
    // Проверяем что стрим добавлен в реестр
    Stream* found = stream_find_by_id(100);
    TEST_ASSERT(&ctx, found == stream, "Should find stream in registry");
    
    // Проверяем что стрим добавлен к владельцу
    bool owner_has_stream = false;
    for (int i = 0; i < MAX_OUTPUT; i++) {
        if (owner->own_streams[i] == stream) {
            owner_has_stream = true;
            break;
        }
    }
    TEST_ASSERT(&ctx, owner_has_stream, "Owner should have stream in own_streams");
    
    // Очищаем
    stream_delete(stream);
    
    // Проверяем что стрим удален из реестра
    found = stream_find_by_id(100);
    TEST_ASSERT(&ctx, found == NULL, "Should not find stream after deletion");
    
    connection_delete(owner);
    close(fd);

    TEST_REPORT(&ctx, "test_stream_new_delete");
}

bool test_stream_private_new() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_stream_private_new");
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner = make_conn(fd);
    Call* call = call_new(500);
    call_add_participant(call, owner); // Владелец должен быть участником звонка

    Stream* stream = stream_new(200, owner, call);
    TEST_ASSERT(&ctx, stream != NULL, "Private stream should be created");
    TEST_ASSERT(&ctx, stream->call == call, "Stream should reference call");
    TEST_ASSERT(&ctx, stream_is_private(stream), "Stream should be private");
    
    // Проверяем что стрим добавлен в call
    bool call_has_stream = false;
    for (int i = 0; i < MAX_CALL_STREAMS; i++) {
        if (call->streams[i] == stream) {
            call_has_stream = true;
            break;
        }
    }
    TEST_ASSERT(&ctx, call_has_stream, "Call should have stream in streams array");
    
    // Очищаем
    stream_delete(stream);
    call_delete(call);
    connection_delete(owner);
    close(fd);

    TEST_REPORT(&ctx, "test_stream_private_new");
}

bool test_stream_recipient_management() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_stream_recipient_management");
    
    int fd1 = socket(AF_INET, SOCK_STREAM, 0);
    int fd2 = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner = make_conn(fd1);
    Connection* recipient = make_conn(fd2);

    // Устанавливаем UDP адрес для получателя (требуется для добавления)
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(9091);
    inet_pton(AF_INET, "127.0.0.1", &udp_addr.sin_addr);
    connection_set_udp_addr(recipient, &udp_addr);
    connection_set_udp_handshake_complete(recipient);

    Stream* stream = stream_new(300, owner, NULL);

    // Добавляем получателя
    int result = stream_add_recipient(stream, recipient);
    TEST_ASSERT(&ctx, result == 0, "Should add recipient");
    TEST_ASSERT(&ctx, stream_has_recipient(stream, recipient), "Stream should have recipient");
    
    // Проверяем счетчик
    int count = stream_get_recipient_count(stream);
    TEST_ASSERT(&ctx, count == 1, "Should have 1 recipient");
    
    // Проверяем что стрим добавлен в watch_streams получателя
    bool recipient_has_stream = false;
    for (int i = 0; i < MAX_INPUT; i++) {
        if (recipient->watch_streams[i] == stream) {
            recipient_has_stream = true;
            break;
        }
    }
    TEST_ASSERT(&ctx, recipient_has_stream, "Recipient should have stream in watch_streams");
    
    // Удаляем получателя
    result = stream_remove_recipient(stream, recipient);
    TEST_ASSERT(&ctx, result == 0, "Should remove recipient");
    TEST_ASSERT(&ctx, !stream_has_recipient(stream, recipient), "Stream should not have recipient");
    
    count = stream_get_recipient_count(stream);
    TEST_ASSERT(&ctx, count == 0, "Should have 0 recipients after removal");

    // Очищаем
    stream_delete(stream);
    connection_delete(owner);
    connection_delete(recipient);
    close(fd1);
    close(fd2);

    TEST_REPORT(&ctx, "test_stream_recipient_management");
}

bool test_stream_find_functions() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_stream_find_functions");
    
    int fd1 = socket(AF_INET, SOCK_STREAM, 0);
    int fd2 = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner1 = make_conn(fd1);
    Connection* owner2 = make_conn(fd2);
    Connection* recipient = make_conn(socket(AF_INET, SOCK_STREAM, 0));

    // Устанавливаем UDP для получателя
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(9092);
    inet_pton(AF_INET, "127.0.0.1", &udp_addr.sin_addr);
    connection_set_udp_addr(recipient, &udp_addr);
    connection_set_udp_handshake_complete(recipient);

    Stream* stream1 = stream_new(400, owner1, NULL);
    Stream* stream2 = stream_new(401, owner2, NULL);

    // Добавляем получателя к stream1
    stream_add_recipient(stream1, recipient);

    // Тестируем поиск по владельцу
    Stream* owner_results[2];
    int count = stream_find_by_owner(owner1, owner_results, 2);
    TEST_ASSERT(&ctx, count == 1, "Should find 1 stream for owner1");
    TEST_ASSERT(&ctx, owner_results[0] == stream1, "Should find correct stream for owner1");
    
    count = stream_find_by_owner(owner2, owner_results, 2);
    TEST_ASSERT(&ctx, count == 1, "Should find 1 stream for owner2");
    TEST_ASSERT(&ctx, owner_results[0] == stream2, "Should find correct stream for owner2");

    // Тестируем поиск по получателю
    Stream* recipient_results[2];
    count = stream_find_by_recipient(recipient, recipient_results, 2);
    TEST_ASSERT(&ctx, count == 1, "Should find 1 stream for recipient");
    TEST_ASSERT(&ctx, recipient_results[0] == stream1, "Should find correct stream for recipient");

    // Очищаем
    stream_delete(stream1);
    stream_delete(stream2);
    connection_delete(owner1);
    connection_delete(owner2);
    connection_delete(recipient);
    close(fd1);
    close(fd2);

    TEST_REPORT(&ctx, "test_stream_find_functions");
}

bool test_stream_delete_cleanup() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_stream_delete_cleanup");
    
    int fd1 = socket(AF_INET, SOCK_STREAM, 0);
    int fd2 = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner = make_conn(fd1);
    Connection* recipient = make_conn(fd2);

    // Устанавливаем UDP для получателя
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(9093);
    inet_pton(AF_INET, "127.0.0.1", &udp_addr.sin_addr);
    connection_set_udp_addr(recipient, &udp_addr);
    connection_set_udp_handshake_complete(recipient);

    Stream* stream = stream_new(500, owner, NULL);
    stream_add_recipient(stream, recipient);

    // Проверяем что связи установлены
    TEST_ASSERT(&ctx, stream_has_recipient(stream, recipient), "Stream should have recipient");
    
    bool recipient_has_stream = false;
    for (int i = 0; i < MAX_INPUT; i++) {
        if (recipient->watch_streams[i] == stream) {
            recipient_has_stream = true;
            break;
        }
    }
    TEST_ASSERT(&ctx, recipient_has_stream, "Recipient should have stream in watch_streams");
    
    bool owner_has_stream = false;
    for (int i = 0; i < MAX_OUTPUT; i++) {
        if (owner->own_streams[i] == stream) {
            owner_has_stream = true;
            break;
        }
    }
    TEST_ASSERT(&ctx, owner_has_stream, "Owner should have stream in own_streams");

    // Удаляем стрим
    stream_delete(stream);

    // Проверяем что связи разорваны
    recipient_has_stream = false;
    for (int i = 0; i < MAX_INPUT; i++) {
        if (recipient->watch_streams[i] == stream) {
            recipient_has_stream = true;
            break;
        }
    }
    TEST_ASSERT(&ctx, !recipient_has_stream, "Recipient should not have stream after deletion");
    
    owner_has_stream = false;
    for (int i = 0; i < MAX_OUTPUT; i++) {
        if (owner->own_streams[i] == stream) {
            owner_has_stream = true;
            break;
        }
    }
    TEST_ASSERT(&ctx, !owner_has_stream, "Owner should not have stream after deletion");

    // Проверяем что стрим удален из реестра
    Stream* found = stream_find_by_id(500);
    TEST_ASSERT(&ctx, found == NULL, "Should not find stream in registry after deletion");

    connection_delete(owner);
    connection_delete(recipient);
    close(fd1);
    close(fd2);

    TEST_REPORT(&ctx, "test_stream_delete_cleanup");
}

bool run_all_stream_tests() {
    printf("Running stream tests...\n\n");
    
    bool all_passed = true;
    all_passed = test_stream_new_delete() && all_passed;
    all_passed = test_stream_private_new() && all_passed;
    all_passed = test_stream_recipient_management() && all_passed;
    all_passed = test_stream_find_functions() && all_passed;
    all_passed = test_stream_delete_cleanup() && all_passed;
    
    if (all_passed) {
        printf("All stream tests passed! ✓\n\n");
    } else {
        printf("Some stream tests failed! ✗\n\n");
    }
    
    return all_passed;
}