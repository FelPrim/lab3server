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
    const int fd;

    Buffer read_buffer;
    Buffer write_buffer;

    struct sockaddr_in tcp_addr;
    struct sockaddr_in udp_addr;

    Stream* watch_streams[MAX_INPUT];
    Stream* own_streams[MAX_OUTPUT];
    Call* calls[MAX_CONNECTION_CALLS];

    UT_hash_handle hh;
} Connection;

extern Connection* connections;

Connection* connection_create(int fd, const struct sockaddr_in* addr);

void connection_add(Connection* conn);
Connection* connection_find(int fd);
void connection_remove(int fd); // не удаляет conn
void connection_close_all(void);

int connection_read_data(Connection* conn);
int connection_write_data(Connection* conn);
int connection_send_message(Connection* conn, const void* data, size_t len);

void connection_set_udp_addr(Connection* conn, const struct sockaddr_in* udp_addr);
bool connection_has_udp(const Connection* conn);

int  connection_add_watch_stream(Connection* conn, Stream* stream);
int  connection_remove_watch_stream(Connection* conn, Stream* stream);
bool connection_is_watching_stream(const Connection* conn, const Stream* stream);
int  connection_get_watch_stream_count(const Connection* conn);

int  connection_add_own_stream(Connection* conn, Stream* stream);
int  connection_remove_own_stream(Connection* conn, Stream* stream);
bool connection_is_owning_stream(const Connection* conn, const Stream* stream);
int  connection_get_own_stream_count(const Connection* conn);

int connection_add_call(Connection* conn, Call* call);
int connection_remove_call(Connection* conn, Call* call);
bool connection_is_in_call(const Connection* conn, const Call* call);
int connection_get_call_count(const Connection* conn);

Stream* connection_find_stream_by_id(const Connection* conn, uint32_t stream_id);
Call* connection_find_call_by_id(const Connection* conn, uint32_t call_id);
int connection_get_stream_count(const Connection* conn);
int connection_get_all_streams(const Connection* conn, Stream** result, int max_results);
int connection_get_all_calls(const Connection* conn, Call** result, int max_results);

const char* connection_get_address_string(const Connection* conn);

int connection_logic_add_own_stream(Connection* conn, Stream* stream);
int connection_logic_remove_own_stream(Connection* conn, Stream* stream);
int connection_logic_add_watch_stream(Connection* conn, Stream* stream);
int connection_logic_remove_watch_stream(Connection* conn, Stream* stream);

// Полное уничтожение соединения
void connection_destroy_full(Connection* conn);
