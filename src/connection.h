#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdbool.h>
#include "buffer.h"
#include "uthash.h"

#define MAX_INPUT 4
#define MAX_OUTPUT 4

// Forward declarations
typedef struct Stream Stream;

typedef struct Connection {
    int fd;                           // файловый дескриптор (ключ)
    Buffer read_buffer;               // буфер для входящих TCP данных
    Buffer write_buffer;              // буфер для исходящих TCP данных
    
    // Сетевые адреса
    struct sockaddr_in tcp_addr;      // TCP адрес клиента  
    struct sockaddr_in udp_addr;      // UDP адрес для видео
    
    // Трансляции (плотные массивы указателей)
    Stream* watch_streams[MAX_INPUT];   // просматриваемые трансляции (NULL-terminated / плотный)
    Stream* own_streams[MAX_OUTPUT];    // управляемые трансляции (NULL-terminated / плотный)
    
    UT_hash_handle hh;
} Connection;

/* Глобальная таблица соединений */
extern Connection* connections;

/* Создание/удаление соединения.
 * connection_create возвращает NULL при ошибке (например, malloc). */
Connection* connection_create(int fd, const struct sockaddr_in* addr);
void connection_destroy(Connection* conn);

/* Управление в глобальной таблице */
int connection_add(Connection* conn); /* возвращает 0 при успехе, <0 при ошибке */
Connection* connection_find(int fd);
int connection_remove(int fd); /* возвращает 0 при успехе, <0 при ошибке */
void connection_close_all(void);

/* Чтение/запись */
int connection_read_data(Connection* conn);
int connection_write_data(Connection* conn);
int connection_send_message(Connection* conn, const void* data, size_t len);

/* UDP адрес */
int connection_set_udp_addr(Connection* conn, const struct sockaddr_in* udp_addr);
bool connection_has_udp(const Connection* conn);

/* Управление просматриваемыми трансляциями */
int connection_add_watch_stream(Connection* conn, Stream* stream);
int connection_remove_watch_stream(Connection* conn, Stream* stream);
bool connection_is_watching_stream(const Connection* conn, const Stream* stream);
int connection_get_watch_stream_count(const Connection* conn);

/* Управление управляемыми трансляциями */
int connection_add_own_stream(Connection* conn, Stream* stream);
int connection_remove_own_stream(Connection* conn, Stream* stream);
bool connection_is_owning_stream(const Connection* conn, const Stream* stream);
int connection_get_own_stream_count(const Connection* conn);

/* Поиск стрима в соединении */
Stream* connection_find_stream_by_id(const Connection* conn, uint32_t stream_id);
int connection_get_stream_count(const Connection* conn);
int connection_get_all_streams(const Connection* conn, Stream** result, int max_results);

/* Утилиты */
const char* connection_get_address_string(const Connection* conn);
