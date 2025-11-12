#include "connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include "stream.h"
#include "network.h"

Connection* connections = NULL;

Connection* connection_create(int fd, const struct sockaddr_in* addr) {
    Connection* conn = (Connection*)malloc(sizeof(Connection));
    if (!conn) return NULL;
    conn->fd = fd;
    buffer_init(&conn->read_buffer);
    buffer_init(&conn->write_buffer);
    if (addr) memcpy(&conn->tcp_addr, addr, sizeof(struct sockaddr_in));
    else memset(&conn->tcp_addr, 0, sizeof(struct sockaddr_in));
    memset(&conn->udp_addr, 0, sizeof(struct sockaddr_in));
    for (int i = 0; i < MAX_INPUT; ++i) conn->watch_streams[i] = NULL;
    for (int i = 0; i < MAX_OUTPUT; ++i) conn->own_streams[i] = NULL;
    return conn;
}

void connection_destroy(Connection* conn) {
    if (!conn) return;

    /* Создаем копии массивов, чтобы безопасно изменять оригинал во время итерации */
    Stream* watch_copy[MAX_INPUT];
    Stream* own_copy[MAX_OUTPUT];
    int watch_count = 0;
    int own_count = 0;

    for (int i = 0; i < MAX_INPUT && conn->watch_streams[i] != NULL; ++i) {
        watch_copy[watch_count++] = conn->watch_streams[i];
    }
    for (int i = 0; i < MAX_OUTPUT && conn->own_streams[i] != NULL; ++i) {
        own_copy[own_count++] = conn->own_streams[i];
    }

    /* Удаляем connection из всех просматриваемых стримов */
    for (int i = 0; i < watch_count; ++i) {
        Stream* s = watch_copy[i];
        if (s) {
            stream_remove_recipient(s, conn);
        }
    }

    /* Для всех собственных стримов: если стрим всё ещё в реестре — уничтожаем его.
       stream_destroy сам удаляет из реестра и очищает связи. */
    for (int i = 0; i < own_count; ++i) {
        Stream* s = own_copy[i];
        if (!s) continue;
        Stream* reg = stream_find_by_id(s->stream_id);
        if (reg == s) {
            stream_destroy(s);
        } else {
            /* Если стрима в реестре нет — возможно, он уже был уничтожен ранее.
               Ничего больше не делаем. */
        }
    }

    /* Закрываем дескриптор и чистим буферы */
    if (conn->fd > 0) close(conn->fd);
    buffer_clear(&conn->read_buffer);
    buffer_clear(&conn->write_buffer);

    /* Не делаем HASH_DEL(connections, conn) здесь — удаление из глобальной таблицы
       должны делать функции управления (connection_remove / connection_close_all),
       иначе получится двойное удаление. */

    free(conn);
}

int connection_add(Connection* conn) {
    if (!conn) return -1;
    Connection* found = connection_find(conn->fd);
    if (found) return -2;
    HASH_ADD_INT(connections, fd, conn);
    return 0;
}

Connection* connection_find(int fd) {
    Connection* conn = NULL;
    HASH_FIND_INT(connections, &fd, conn);
    return conn;
}

int connection_remove(int fd) {
    Connection* conn = connection_find(fd);
    if (!conn) return -1;
    HASH_DEL(connections, conn);
    connection_destroy(conn);
    return 0;
}

void connection_close_all(void) {
    Connection *c, *tmp;
    HASH_ITER(hh, connections, c, tmp) {
        HASH_DEL(connections, c);
        connection_destroy(c);
    }
}

/* Операции с сокетами - обёртки */
int connection_read_data(Connection* conn) {
    if (!conn) return -1;
    return buffer_read_socket(&conn->read_buffer, conn->fd);
}

int connection_write_data(Connection* conn) {
    if (!conn) return -1;
    return buffer_write_socket(&conn->write_buffer, conn->fd);
}

int connection_send_message(Connection* conn, const void* data, size_t len) {
    if (!conn || !data) return -1;
    if (len > BUFFER_SIZE) return -1;
    buffer_prepare_send(&conn->write_buffer, data, len);
    return connection_write_data(conn);
}

/* UDP */
int connection_set_udp_addr(Connection* conn, const struct sockaddr_in* udp_addr) {
    if (!conn || !udp_addr) return -1;
    memcpy(&conn->udp_addr, udp_addr, sizeof(struct sockaddr_in));
    return 0;
}

bool connection_has_udp(const Connection* conn) {
    if (!conn) return false;
    return conn->udp_addr.sin_port != 0;
}

/* Управление просматриваемыми трансляциями (только модификация указателей) */
int connection_add_watch_stream(Connection* conn, Stream* stream) {
    if (!conn || !stream) return -1;
    if (connection_is_watching_stream(conn, stream)) return -2;
    for (int i = 0; i < MAX_INPUT; ++i) {
        if (conn->watch_streams[i] == NULL) {
            conn->watch_streams[i] = stream;
            return 0;
        }
    }
    return -3; /* нет места */
}

int connection_remove_watch_stream(Connection* conn, Stream* stream) {
    if (!conn || !stream) return -1;
    for (int i = 0; i < MAX_INPUT; ++i) {
        if (conn->watch_streams[i] == stream) {
            /* Плотный массив: сдвигаем влево */
            for (int j = i; j + 1 < MAX_INPUT; ++j) {
                conn->watch_streams[j] = conn->watch_streams[j + 1];
            }
            conn->watch_streams[MAX_INPUT - 1] = NULL;
            return 0;
        }
    }
    return -2; /* не найден */
}

bool connection_is_watching_stream(const Connection* conn, const Stream* stream) {
    if (!conn || !stream) return false;
    for (int i = 0; i < MAX_INPUT && conn->watch_streams[i] != NULL; ++i) {
        if (conn->watch_streams[i] == stream) return true;
    }
    return false;
}

int connection_get_watch_stream_count(const Connection* conn) {
    if (!conn) return 0;
    int c = 0;
    for (int i = 0; i < MAX_INPUT; ++i) if (conn->watch_streams[i] != NULL) ++c;
    return c;
}

/* Управление управляемыми трансляциями (только модификация указателей) */
int connection_add_own_stream(Connection* conn, Stream* stream) {
    if (!conn || !stream) return -1;
    if (connection_is_owning_stream(conn, stream)) return -2;
    for (int i = 0; i < MAX_OUTPUT; ++i) {
        if (conn->own_streams[i] == NULL) {
            conn->own_streams[i] = stream;
            return 0;
        }
    }
    return -3;
}

int connection_remove_own_stream(Connection* conn, Stream* stream) {
    if (!conn || !stream) return -1;
    for (int i = 0; i < MAX_OUTPUT; ++i) {
        if (conn->own_streams[i] == stream) {
            for (int j = i; j + 1 < MAX_OUTPUT; ++j) {
                conn->own_streams[j] = conn->own_streams[j + 1];
            }
            conn->own_streams[MAX_OUTPUT - 1] = NULL;
            return 0;
        }
    }
    return -2;
}

bool connection_is_owning_stream(const Connection* conn, const Stream* stream) {
    if (!conn || !stream) return false;
    for (int i = 0; i < MAX_OUTPUT && conn->own_streams[i] != NULL; ++i) {
        if (conn->own_streams[i] == stream) return true;
    }
    return false;
}

int connection_get_own_stream_count(const Connection* conn) {
    if (!conn) return 0;
    int c = 0;
    for (int i = 0; i < MAX_OUTPUT; ++i) if (conn->own_streams[i] != NULL) ++c;
    return c;
}

/* Поиск стрима в соединении */
Stream* connection_find_stream_by_id(const Connection* conn, uint32_t stream_id) {
    if (!conn) return NULL;
    for (int i = 0; i < MAX_OUTPUT && conn->own_streams[i] != NULL; ++i) {
        if (conn->own_streams[i]->stream_id == stream_id) return conn->own_streams[i];
    }
    for (int i = 0; i < MAX_INPUT && conn->watch_streams[i] != NULL; ++i) {
        if (conn->watch_streams[i]->stream_id == stream_id) return conn->watch_streams[i];
    }
    return NULL;
}

int connection_get_stream_count(const Connection* conn) {
    if (!conn) return 0;
    return connection_get_watch_stream_count(conn) + connection_get_own_stream_count(conn);
}

int connection_get_all_streams(const Connection* conn, Stream** result, int max_results) {
    if (!conn || !result || max_results <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MAX_INPUT && count < max_results; ++i) {
        if (conn->watch_streams[i] != NULL) result[count++] = conn->watch_streams[i];
    }
    for (int i = 0; i < MAX_OUTPUT && count < max_results; ++i) {
        if (conn->own_streams[i] != NULL) result[count++] = conn->own_streams[i];
    }
    return count;
}

/* Утилиты */
const char* connection_get_address_string(const Connection* conn) {
    static char buffer[64];
    if (!conn) {
        return "<null>";
    }
    sockaddr_to_string(&conn->tcp_addr, buffer, sizeof(buffer));
    return buffer;
}
