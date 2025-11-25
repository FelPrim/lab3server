#include "connection.h"
#include "stream.h"
#include "call.h"
#include "buffer_logic.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

extern int g_epoll_fd;
Connection* connections = NULL;

/* Внутренние функции */
static Connection* connection_alloc(int fd, const struct sockaddr_in* addr) {
    Connection* conn = malloc(sizeof(Connection));
    if (!conn) return NULL;

    conn->fd = fd;
    
    buffer_init(&conn->read_buffer);
    buffer_init(&conn->write_buffer);

    if (addr) {
        memcpy(&conn->tcp_addr, addr, sizeof(struct sockaddr_in));
    }

    conn->udp_handshake_complete = false;
    
    DENSE_ARRAY_INIT(conn->watch_streams, MAX_INPUT);
    DENSE_ARRAY_INIT(conn->own_streams, MAX_OUTPUT);
    DENSE_ARRAY_INIT(conn->calls, MAX_CONNECTION_CALLS);

    return conn;
}

int connection_remove_call_safe(Connection* conn, Call* call) {
    if (!conn || !call) return 0; // Идемпотентность
    
    // Находим индекс и устанавливаем в NULL
    int index = DENSE_ARRAY_INDEX_OF(conn->calls, MAX_CONNECTION_CALLS, call);
    if (index >= 0) {
        conn->calls[index] = NULL;
        return 0;
    }
    return -1;
}

static void connection_free(Connection* conn) {
    if (!conn) return;
    free(conn);
}

static void connection_close(Connection* conn) {
    if (!conn) return;
    if (conn->fd >= 0) {
        close(conn->fd);
    }
}

static void connection_detach_from_streams(Connection* conn) {
    if (!conn) return;

    // Создаем временный массив для безопасного удаления
    Stream* streams_to_detach[MAX_INPUT];
    int count = 0;
    
    for (int i = 0; i < MAX_INPUT; i++) {
        if (conn->watch_streams[i] != NULL) {
            streams_to_detach[count++] = conn->watch_streams[i];
        }
    }
    
    // Отписываем от всех стримов
    for (int i = 0; i < count; ++i) {
        if (streams_to_detach[i]) {
            stream_remove_recipient(streams_to_detach[i], conn);
        }
    }
}

static void connection_detach_from_calls(Connection* conn) {
    if (!conn) return;

    // Создаем временный массив для безопасного удаления
    Call* calls_to_detach[MAX_CONNECTION_CALLS];
    int count = 0;
    
    for (int i = 0; i < MAX_CONNECTION_CALLS; i++) {
        if (conn->calls[i] != NULL) {
            calls_to_detach[count++] = conn->calls[i];
        }
    }
    
    // Выходим из всех звонков
    for (int i = 0; i < count; ++i) {
        if (calls_to_detach[i]) {
            call_remove_participant(calls_to_detach[i], conn);
        }
    }
}

static void connection_delete_owned_streams(Connection* conn) {
    if (!conn) return;

    // Создаем временный массив для безопасного удаления
    Stream* owned_streams[MAX_OUTPUT];
    int count = 0;
    
    for (int i = 0; i < MAX_OUTPUT; i++) {
        if (conn->own_streams[i] != NULL) {
            owned_streams[count++] = conn->own_streams[i];
        }
    }
    
    // Удаляем все стримы
    for (int i = 0; i < count; ++i) {
        if (owned_streams[i]) {
            printf("Destroying owned stream %u for connection %d\n", 
                   owned_streams[i]->stream_id, conn->fd);
            stream_delete(owned_streams[i]);
        }
    }
}

static void connection_add(Connection* conn) {
    if (!conn) return;
    HASH_ADD_INT(connections, fd, conn);
}


/* Публичные функции */
Connection* connection_new(int fd, const struct sockaddr_in* addr) {
    Connection* conn = connection_alloc(fd, addr);
    if (!conn) return NULL;
    
    // Добавляем в глобальную хеш-таблицу
    connection_add(conn);
    
    return conn;
}

void connection_delete(Connection* conn) {
    if (!conn) return;

    printf("Destroying connection %d\n", conn->fd);

    // 1. Отписываемся от просматриваемых стримов (синхронно)
    for (int i = MAX_INPUT - 1; i >= 0; i--) {
        if (conn->watch_streams[i] != NULL) {
            Stream* stream = conn->watch_streams[i];
            // Сначала удаляем из стрима, потом очищаем слот
            if (stream_remove_recipient(stream, conn) == 0) {
                conn->watch_streams[i] = NULL;
            }
        }
    }

    // 2. Удаляем собственные стримы (синхронно)  
    for (int i = MAX_OUTPUT - 1; i >= 0; i--) {
        if (conn->own_streams[i] != NULL) {
            Stream* stream = conn->own_streams[i];
            // Удаляем стрим и очищаем слот
            stream_delete(stream);
            conn->own_streams[i] = NULL;
        }
    }

    // 3. Выходим из всех звонков (синхронно)
    for (int i = MAX_CONNECTION_CALLS - 1; i >= 0; i--) {
        if (conn->calls[i] != NULL) {
            Call* call = conn->calls[i];
            // Используем безопасное удаление с обеих сторон
            call_remove_participant_safe(call, conn);
            connection_remove_call_safe(conn, call);
        }
    }

    // 4. Удаляем из глобальной хеш-таблицы
    if (conn->fd >= 0) {
        Connection* found = NULL;
        HASH_FIND_INT(connections, &conn->fd, found);
        if (found == conn) {
            HASH_DEL(connections, conn);
        }
    }

    // 5. Закрываем сокет и освобождаем память
    connection_close(conn);
    connection_free(conn);
}

Connection* connection_find(int fd) {
    Connection* result = NULL;
    HASH_FIND_INT(connections, &fd, result);
    return result;
}


void connection_close_all(void) {
    Connection* current;
    Connection* tmp;
    
    HASH_ITER(hh, connections, current, tmp) {
        connection_delete(current);
    }
    
    connections = NULL;
}

int connection_read_data(Connection* conn) {
    if (!conn) return -1;

    uint8_t temp[512];
    ssize_t n = read(conn->fd, temp, sizeof(temp));

    if (n > 0) {
        BufferResult res = buffer_write(&conn->read_buffer, temp, (uint32_t)n);
        if (res == BUFFER_OVERFLOW) {
            fprintf(stderr, "connection_read_data: buffer overflow\n");
            buffer_clear(&conn->read_buffer);
            return -1;
        }
        return (int)n;
    } else if (n == 0) {
        return 0; // соединение закрыто
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -2;
        return -1;
    }
}

int connection_write_data(Connection* conn) {
    if (!conn) return -1;

    if (conn->write_buffer.position == 0)
        return 0; // нечего писать

    ssize_t n = write(conn->fd,
                      conn->write_buffer.data,
                      conn->write_buffer.position);

    if (n > 0) {
        uint32_t remaining = conn->write_buffer.position - n;
        if (remaining > 0) {
            memmove(conn->write_buffer.data, conn->write_buffer.data + n, remaining);
        }
        conn->write_buffer.position = remaining;

        if (conn->write_buffer.position == 0) {
            buffer_clear(&conn->write_buffer);
            return 1; // все данные записаны
        }
        return -2; // записано частично
    } else if (n == 0) {
        return 0; // соединение закрыто
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -2; // нужно повторить позже
        return -1; // ошибка
    }
}

int connection_send_message(Connection* conn, const void* data, size_t len) {
    if (!conn || !data || len == 0)
        return -1;
    if (len > BUFFER_SIZE)
        return -1;

    buffer_clear(&conn->write_buffer);
    buffer_reserve(&conn->write_buffer, (uint32_t)len);
    buffer_write(&conn->write_buffer, data, (uint32_t)len);

    int result = connection_write_data(conn);
    
    if (result == -2) { // EAGAIN/EWOULDBLOCK
        epoll_modify(g_epoll_fd, conn->fd, EPOLLIN | EPOLLOUT | EPOLLET);
    }
    
    return result;
}

void connection_set_udp_addr(Connection* conn, const struct sockaddr_in* udp_addr) {
    if (!conn || !udp_addr) return;
    memcpy(&conn->udp_addr, udp_addr, sizeof(struct sockaddr_in));
}

bool connection_has_udp(const Connection* conn) {
    return conn && conn->udp_addr.sin_port != 0;
}

bool connection_is_udp_handshake_complete(const Connection* conn) {
    return conn && conn->udp_handshake_complete;
}

void connection_set_udp_handshake_complete(Connection* conn) {
    if (conn) {
        conn->udp_handshake_complete = true;
    }
}

int connection_add_watch_stream(Connection* conn, Stream* stream) {
    if (!conn || !stream) return -1;
    if (connection_is_watching_stream(conn, stream)) return -2;
    return DENSE_ARRAY_ADD(conn->watch_streams, MAX_INPUT, stream);
}

int connection_remove_watch_stream(Connection* conn, Stream* stream) {
    if (!conn || !stream) return -1;
    return DENSE_ARRAY_REMOVE(conn->watch_streams, MAX_INPUT, stream);
}

bool connection_is_watching_stream(const Connection* conn, const Stream* stream) {
    if (!conn || !stream) return false;
    return DENSE_ARRAY_CONTAINS(conn->watch_streams, MAX_INPUT, stream);
}

int connection_add_own_stream(Connection* conn, Stream* stream) {
    if (!conn || !stream) return -1;
    if (connection_is_owning_stream(conn, stream)) return -2;
    return DENSE_ARRAY_ADD(conn->own_streams, MAX_OUTPUT, stream);
}

int connection_remove_own_stream(Connection* conn, Stream* stream) {
    if (!conn || !stream) return -1;
    return DENSE_ARRAY_REMOVE(conn->own_streams, MAX_OUTPUT, stream);
}

bool connection_is_owning_stream(const Connection* conn, const Stream* stream) {
    if (!conn || !stream) return false;
    return DENSE_ARRAY_CONTAINS(conn->own_streams, MAX_OUTPUT, stream);
}

int connection_add_call(Connection* conn, Call* call) {
    if (!conn || !call) return -1;
    if (connection_is_in_call(conn, call)) return -2;
    return DENSE_ARRAY_ADD(conn->calls, MAX_CONNECTION_CALLS, call);
}

int connection_remove_call(Connection* conn, Call* call) {
    if (!conn || !call) return 0; // Становится идемпотентной
    return DENSE_ARRAY_REMOVE(conn->calls, MAX_CONNECTION_CALLS, call);
}

bool connection_is_in_call(const Connection* conn, const Call* call) {
    if (!conn || !call) return false;
    return DENSE_ARRAY_CONTAINS(conn->calls, MAX_CONNECTION_CALLS, call);
}

int connection_get_call_count(const Connection* conn) {
    if (!conn) return 0;
    return (int)DENSE_ARRAY_COUNT(conn->calls, MAX_CONNECTION_CALLS);
}

Stream* connection_find_stream_by_id(const Connection* conn, uint32_t stream_id) {
    if (!conn) return NULL;
    
    for (int i = 0; i < MAX_OUTPUT; ++i) {
        if (conn->own_streams[i] != NULL && conn->own_streams[i]->stream_id == stream_id) {
            return conn->own_streams[i];
        }
    }
    
    for (int i = 0; i < MAX_INPUT; ++i) {
        if (conn->watch_streams[i] != NULL && conn->watch_streams[i]->stream_id == stream_id) {
            return conn->watch_streams[i];
        }
    }
    
    return NULL;
}

Call* connection_find_call_by_id(const Connection* conn, uint32_t call_id) {
    if (!conn) return NULL;
    
    for (int i = 0; i < MAX_CONNECTION_CALLS; ++i) {
        if (conn->calls[i] != NULL && conn->calls[i]->call_id == call_id) {
            return conn->calls[i];
        }
    }
    
    return NULL;
}

const char* connection_get_address_string(const Connection* conn) {
    static char buffer[64];
    if (!conn) {
        return "<null>";
    }
    
    const char* ip = inet_ntoa(conn->tcp_addr.sin_addr);
    uint16_t port = ntohs(conn->tcp_addr.sin_port);
    snprintf(buffer, sizeof(buffer), "%s:%d", ip, port);
    
    return buffer;
}


bool connection_can_add_own_stream(const Connection* conn) {
    if (!conn) return false;
    
    for (int i = 0; i < MAX_OUTPUT; i++) {
        if (conn->own_streams[i] == NULL) {
            return true;
        }
    }
    return false;
}

bool connection_can_add_watch_stream(const Connection* conn) {
    if (!conn) return false;
    
    for (int i = 0; i < MAX_INPUT; i++) {
        if (conn->watch_streams[i] == NULL) {
            return true;
        }
    }
    return false;
}

bool connection_can_add_call(const Connection* conn) {
    if (!conn) return false;
    
    for (int i = 0; i < MAX_CONNECTION_CALLS; i++) {
        if (conn->calls[i] == NULL) {
            return true;
        }
    }
    return false;
}