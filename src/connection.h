#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdbool.h>
#include "buffer.h"  
#include "uthash.h"
#include "dense_array.h"

#define MAX_INPUT 4
#define MAX_OUTPUT 4
#define MAX_CONNECTION_CALLS 4

typedef struct Stream Stream;
typedef struct Call Call;

typedef struct Connection {
    int fd;

    Buffer read_buffer;
    Buffer write_buffer;

    struct sockaddr_in tcp_addr;
    struct sockaddr_in udp_addr;
    bool udp_handshake_complete;

    Stream* watch_streams[MAX_INPUT];
    Stream* own_streams[MAX_OUTPUT];
    Call* calls[MAX_CONNECTION_CALLS];

    UT_hash_handle hh;
} Connection;

extern Connection* connections;

/* Основные операции жизненного цикла */
Connection* connection_new(int fd, const struct sockaddr_in* addr);
void connection_delete(Connection* conn);

Connection* connection_find(int fd);
void connection_close_all(void);

/* Сетевые операции */
int connection_read_data(Connection* conn);
int connection_write_data(Connection* conn);
int connection_send_message(Connection* conn, const void* data, size_t len);

/* UDP */
void connection_set_udp_addr(Connection* conn, const struct sockaddr_in* udp_addr);
bool connection_has_udp(const Connection* conn);
bool connection_is_udp_handshake_complete(const Connection* conn);
void connection_set_udp_handshake_complete(Connection* conn);

/* Управление стримами */
int connection_add_watch_stream(Connection* conn, Stream* stream);
int connection_remove_watch_stream(Connection* conn, Stream* stream);
bool connection_is_watching_stream(const Connection* conn, const Stream* stream);

int connection_add_own_stream(Connection* conn, Stream* stream);
int connection_remove_own_stream(Connection* conn, Stream* stream);
bool connection_is_owning_stream(const Connection* conn, const Stream* stream);

/* Управление звонками */
int connection_add_call(Connection* conn, Call* call);
int connection_remove_call(Connection* conn, Call* call);
bool connection_is_in_call(const Connection* conn, const Call* call);
int connection_get_call_count(const Connection* conn);

/* Поиск */
Stream* connection_find_stream_by_id(const Connection* conn, uint32_t stream_id);
Call* connection_find_call_by_id(const Connection* conn, uint32_t call_id);

/* Утилиты */
const char* connection_get_address_string(const Connection* conn);

bool connection_can_add_own_stream(const Connection* conn);
bool connection_can_add_watch_stream(const Connection* conn);
bool connection_can_add_call(const Connection* conn);
int connection_remove_call_safe(Connection* conn, Call* call);