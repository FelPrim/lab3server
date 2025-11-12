// test/test_connection.c
// Подробные unit-тесты для connection.c / connection.h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "connection.h"
#include "buffer.h"
#include "stream.h"

static Connection* make_conn(int fd) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    Connection* c = connection_create(fd, &addr);
    assert(c != NULL);
    return c;
}

void test_connection_create_destroy() {
    printf("=== test_connection_create_destroy ===\n");
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    Connection* c = make_conn(fd);
    assert(c->fd == fd);
    assert(buffer_get_data_size(&c->read_buffer) == 0);
    assert(buffer_get_data_size(&c->write_buffer) == 0);
    for (int i = 0; i < MAX_INPUT; ++i) assert(c->watch_streams[i] == NULL);
    for (int i = 0; i < MAX_OUTPUT; ++i) assert(c->own_streams[i] == NULL);
    connection_destroy(c);
    printf("✓ passed\n\n");
}

void test_connection_global_table_and_duplicates() {
    printf("=== test_connection_global_table_and_duplicates ===\n");
    int fd1 = socket(AF_INET, SOCK_STREAM, 0);
    int fd2 = socket(AF_INET, SOCK_STREAM, 0);
    Connection* a = make_conn(fd1);
    Connection* b = make_conn(fd2);

    assert(connection_add(a) == 0);
    assert(connection_add(b) == 0);
    /* duplicate add should fail */
    assert(connection_add(a) < 0);

    Connection* f = connection_find(fd1);
    assert(f == a);
    f = connection_find(fd2);
    assert(f == b);

    assert(connection_remove(fd1) == 0);
    assert(connection_find(fd1) == NULL);

    /* remove non-existent should return error */
    assert(connection_remove(999999) < 0);

    /* cleanup */
    assert(connection_remove(fd2) == 0);
    printf("✓ passed\n\n");
}

void test_connection_udp_and_io_wrappers() {
    printf("=== test_connection_udp_and_io_wrappers ===\n");
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    Connection* c = make_conn(fds[0]);

    /* UDP addr */
    struct sockaddr_in udp;
    memset(&udp, 0, sizeof(udp));
    udp.sin_family = AF_INET;
    udp.sin_port = htons(9000);
    inet_pton(AF_INET, "10.0.0.5", &udp.sin_addr);
    assert(connection_set_udp_addr(c, &udp) == 0);
    assert(connection_has_udp(c) == true);
    assert(c->udp_addr.sin_port == udp.sin_port);

    /* send message (buffer_prepare_send + write) */
    const char* msg = "test";
    int rc = connection_send_message(c, msg, strlen(msg));
    assert(rc >= 0);

    /* read using other end */
    char buf[32];
    ssize_t n = read(fds[1], buf, sizeof(buf));
    assert(n >= 0);

    const char* resp = "hello";
    write(fds[1], resp, strlen(resp));
    int rr = connection_read_data(c);
    assert(rr >= 0);

    close(fds[1]);
    connection_destroy(c);
    printf("✓ passed\n\n");
}

void test_connection_stream_ownership_and_cascade() {
    printf("=== test_connection_stream_ownership_and_cascade ===\n");
    int fd_owner = socket(AF_INET, SOCK_STREAM, 0);
    int fd_view1 = socket(AF_INET, SOCK_STREAM, 0);
    int fd_view2 = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner = make_conn(fd_owner);
    Connection* v1 = make_conn(fd_view1);
    Connection* v2 = make_conn(fd_view2);

    /* owner creates 2 streams */
    Stream* s1 = stream_create(40001, owner);
    Stream* s2 = stream_create(40002, owner);
    assert(s1 && s2);
    assert(connection_get_own_stream_count(owner) == 2);

    /* viewers subscribe */
    assert(connection_add_watch_stream(v1, s1) == 0);
    assert(stream_add_recipient(s1, v1) == 0);

    assert(connection_add_watch_stream(v2, s1) == 0);
    assert(stream_add_recipient(s1, v2) == 0);

    /* Now destroy owner: should cascade - remove s1 and s2 from registry,
       and viewer watch lists should be cleared for s1. */
    connection_destroy(owner);

    /* s1 and s2 should no longer be in registry */
    assert(stream_find_by_id(40001) == NULL);
    assert(stream_find_by_id(40002) == NULL);

    /* viewers must not be watching s1 anymore */
    assert(connection_is_watching_stream(v1, s1) == false);
    assert(connection_is_watching_stream(v2, s1) == false);

    /* cleanup viewers */
    connection_destroy(v1);
    connection_destroy(v2);

    printf("✓ passed\n\n");
}

void test_connection_watch_and_own_limits_and_dense_arrays() {
    printf("=== test_connection_watch_and_own_limits_and_dense_arrays ===\n");
    int fd_owner = socket(AF_INET, SOCK_STREAM, 0);
    Connection* owner = make_conn(fd_owner);

    /* Create MAX_OUTPUT streams for owner */
    Stream* arr[MAX_OUTPUT];
    for (int i = 0; i < MAX_OUTPUT; ++i) {
        arr[i] = stream_create(50000 + i, owner);
        assert(arr[i] != NULL);
    }
    assert(connection_get_own_stream_count(owner) == MAX_OUTPUT);

    /* creating one more for same owner must fail (stream_create returns NULL due to owner limit) */
    Stream* extra = stream_create(60000, owner);
    assert(extra == NULL);

    /* Remove middle one with connection_remove_own_stream (does NOT free stream) */
    Stream* removed = arr[MAX_OUTPUT / 2];
    assert(connection_remove_own_stream(owner, removed) == 0);
    /* owner array should be dense: remaining entries are non-NULL followed by NULLs */
    int cnt = connection_get_own_stream_count(owner);
    assert(cnt == MAX_OUTPUT - 1);

    /* we must free removed stream manually */
    stream_destroy(removed);

    /* cleanup */
    connection_destroy(owner);
    printf("✓ passed\n\n");
}

void test_connection_invalid_args_and_edgecases() {
    printf("=== test_connection_invalid_args_and_edgecases ===\n");
    /* Null safety checks */
    assert(connection_add(NULL) < 0);
    assert(connection_find(-1) == NULL);
    assert(connection_remove(-1) < 0);
    assert(connection_read_data(NULL) < 0);
    assert(connection_write_data(NULL) < 0);
    assert(connection_send_message(NULL, NULL, 0) < 0);
    assert(connection_set_udp_addr(NULL, NULL) < 0);
    assert(connection_has_udp(NULL) == false);
    assert(connection_add_watch_stream(NULL, NULL) < 0);
    assert(connection_remove_watch_stream(NULL, NULL) < 0);
    assert(connection_add_own_stream(NULL, NULL) < 0);
    assert(connection_remove_own_stream(NULL, NULL) < 0);
    printf("✓ passed\n\n");
}

void run_all_connection_tests() {
    printf("Running connection tests...\n\n");
    test_connection_create_destroy();
    test_connection_global_table_and_duplicates();
    test_connection_udp_and_io_wrappers();
    test_connection_stream_ownership_and_cascade();
    test_connection_watch_and_own_limits_and_dense_arrays();
    test_connection_invalid_args_and_edgecases();
    printf("All connection tests passed! ✓\n\n");
}
