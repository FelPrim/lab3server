#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdbool.h>
#include "buffer.h"  
#include "uthash.h"
#include "stream.h"

#define MAX_INPUT 4
#define MAX_OUTPUT 4

// Forward declarations
typedef struct Stream Stream;

typedef struct Connection {
    int fd;

    Buffer read_buffer;
    Buffer write_buffer;

    struct sockaddr_in tcp_addr;
    struct sockaddr_in udp_addr;

    Stream* watch_streams[MAX_INPUT];
    Stream* own_streams[MAX_OUTPUT];

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

Stream* connection_find_stream_by_id(const Connection* conn, uint32_t stream_id);
int connection_get_stream_count(const Connection* conn);
int connection_get_all_streams(const Connection* conn, Stream** result, int max_results);

const char* connection_get_address_string(const Connection* conn);

// Убрано дублирующее объявление connection_destroy_full - оно уже есть в connection_logic.h

// Добавьте в конец connection.h

// Управление стримами через connection_logic
int connection_logic_add_own_stream(Connection* conn, Stream* stream);
int connection_logic_remove_own_stream(Connection* conn, Stream* stream);
int connection_logic_add_watch_stream(Connection* conn, Stream* stream);
int connection_logic_remove_watch_stream(Connection* conn, Stream* stream);

// Полное уничтожение соединения
void connection_destroy_full(Connection* conn);