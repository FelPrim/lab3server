// test/test_stream.c
// Подробные unit-тесты для stream.c / stream.h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "connection.h"
#include "stream.h"

static Connection* make_conn(int fd) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    Connection* c = connection_create(fd, &addr);
    assert(c != NULL);
    return c;
}

void test_stream_create_destroy_basic() {
    printf("=== test_stream_create_destroy_basic ===\n");
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    Connection* owner = make_conn(fd);

    Stream* s = stream_create(10000, owner);
    assert(s != NULL);
    assert(stream_find_by_id(10000) == s);
    assert(connection_is_owning_stream(owner, s) == true);

    /* double destroy should be safe */
    stream_destroy(s);
    /* after destruction registry should not contain it */
    assert(stream_find_by_id(10000) == NULL);
    /* second call is no-op */
    stream_destroy(s);

    connection_destroy(owner);
    printf("✓ passed\n\n");
}

void test_stream_recipients_limits_and_symmetry() {
    printf("=== test_stream_recipients_limits_and_symmetry ===\n");
    int fd_owner = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd_owner >= 0);
    Connection* owner = make_conn(fd_owner);

    Stream* s = stream_create(10001, owner);
    assert(s != NULL);

    int N = STREAM_MAX_RECIPIENTS;
    Connection* viewers[STREAM_MAX_RECIPIENTS + 2];
    for (int i = 0; i < N + 1; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd >= 0);
        viewers[i] = make_conn(fd);
    }

    int ok = 0, fail = 0;
    for (int i = 0; i < N + 1; ++i) {
        int rc_stream = stream_add_recipient(s, viewers[i]);
        int rc_conn = 0;
        if (rc_stream == 0)
            rc_conn = connection_add_watch_stream(viewers[i], s);

        if (rc_stream == 0 && rc_conn == 0)
            ++ok;
        else
            ++fail;
    }
    assert(ok == N);
    assert(fail == 1);

    /* Verify stream_is_recipient and connection_is_watching_stream reflect same state */
    int counted = 0;
    for (int i = 0; i < N + 1; ++i) {
        if (i < N) {
            assert(stream_is_recipient(s, viewers[i]) == 1);
            assert(connection_is_watching_stream(viewers[i], s) == true);
            ++counted;
        } else {
            assert(stream_is_recipient(s, viewers[i]) == 0);
            assert(connection_is_watching_stream(viewers[i], s) == false);
        }
    }
    assert((int)s->recipient_count == counted);

    /* Clearing recipients should remove watchers from their connections */
    stream_clear_recipients(s);
    for (int i = 0; i < N; ++i) {
        assert(stream_is_recipient(s, viewers[i]) == 0);
        assert(connection_is_watching_stream(viewers[i], s) == false);
    }
    assert(s->recipient_count == 0);

    for (int i = 0; i < N + 1; ++i) connection_destroy(viewers[i]);
    stream_destroy(s);
    connection_destroy(owner);
    printf("✓ passed\n\n");
}

void test_stream_registry_and_manual_registry_removal() {
    printf("=== test_stream_registry_and_manual_registry_removal ===\n");
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    Connection* owner = make_conn(fd);

    Stream* a = stream_create(20001, owner);
    Stream* b = stream_create(20002, owner);
    assert(a && b);
    assert(stream_find_by_id(20001) == a);
    assert(stream_find_by_id(20002) == b);

    /* Manually remove b from registry (simulate external action).
       After that stream_destroy(b) must not crash (should be safe no-op). */
    int rm = stream_remove_from_registry(b);
    assert(rm == 0);
    assert(stream_find_by_id(20002) == NULL);

    /* Now call stream_destroy on b - since it's no longer in registry the destroy should be no-op */
    stream_destroy(b); /* safe */

    /* a still exists */
    assert(stream_find_by_id(20001) == a);

    /* Cleanup */
    stream_destroy(a);
    connection_destroy(owner);
    printf("✓ passed\n\n");
}

void test_stream_dense_array_behavior() {
    printf("=== test_stream_dense_array_behavior ===\n");
    int fd_owner = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd_owner >= 0);
    Connection* owner = make_conn(fd_owner);

    /* Create STREAM_MAX_RECIPIENTS viewers and attach them */
    Stream* s = stream_create(30001, owner);
    assert(s != NULL);

    Connection* viewers[STREAM_MAX_RECIPIENTS];
    for (int i = 0; i < STREAM_MAX_RECIPIENTS; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        viewers[i] = make_conn(fd);
        assert(connection_add_watch_stream(viewers[i], s) == 0);
        assert(stream_add_recipient(s, viewers[i]) == 0);
    }
    assert(s->recipient_count == STREAM_MAX_RECIPIENTS);

    /* Remove middle element and check dense property */
    int mid = STREAM_MAX_RECIPIENTS / 2;
    Connection* removed = s->recipients[mid];
    assert(stream_remove_recipient(s, removed) == 0);

    /* After removal, recipient_count decreased by 1 and no gaps */
    assert((int)s->recipient_count == STREAM_MAX_RECIPIENTS - 1);
    for (uint32_t i = 0; i < s->recipient_count; ++i) {
        assert(s->recipients[i] != NULL);
    }
    /* removed should not be in list */
    assert(stream_is_recipient(s, removed) == 0);

    stream_clear_recipients(s);
    for (int i = 0; i < STREAM_MAX_RECIPIENTS; ++i) connection_destroy(viewers[i]);
    stream_destroy(s);
    connection_destroy(owner);
    printf("✓ passed\n\n");
}

void test_stream_invalid_args() {
    printf("=== test_stream_invalid_args ===\n");
    /* NULL checks should be safe and return errors (not crash) */
    assert(stream_add_recipient(NULL, NULL) == -1);
    assert(stream_remove_recipient(NULL, NULL) == -1);
    assert(stream_is_recipient(NULL, NULL) == 0);
    stream_clear_recipients(NULL);
    assert(stream_add_to_registry(NULL) == -1);
    assert(stream_remove_from_registry(NULL) == -1);
    assert(stream_find_by_id(0xffffffffu) == NULL);
    printf("✓ passed\n\n");
}

void run_all_stream_tests() {
    printf("Running stream tests...\n\n");
    test_stream_create_destroy_basic();
    test_stream_recipients_limits_and_symmetry();
    test_stream_registry_and_manual_registry_removal();
    test_stream_dense_array_behavior();
    test_stream_invalid_args();
    printf("All stream tests passed! ✓\n\n");
}
