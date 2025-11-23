#include "connection.h"
#include "buffer_logic.h" 
#include "stream.h"  
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h>  

#include "network.h"

Connection* connections = NULL;

Connection* connection_create(int fd, const struct sockaddr_in* addr) {
    if (fd < 0) return NULL;

    Connection* conn = calloc(1, sizeof(Connection));
    if (!conn) return NULL;

    conn->fd = fd;
    
    // Используем функции из buffer.h для инициализации
    buffer_init(&conn->read_buffer);
    buffer_init(&conn->write_buffer);

    if (addr) {
        memcpy(&conn->tcp_addr, addr, sizeof(struct sockaddr_in));
    }

    // Инициализация массивов
    for (int i = 0; i < MAX_INPUT; i++) {
        conn->watch_streams[i] = NULL;
    }
    for (int i = 0; i < MAX_OUTPUT; i++) {
        conn->own_streams[i] = NULL;
    }

    return conn;
}

void connection_destroy(Connection* conn) {
    if (!conn) return;
    if (conn->fd >= 0) {
        close(conn->fd);
    }
    free(conn);
}

void connection_add(Connection* conn) {
    if (!conn) return;
    HASH_ADD_INT(connections, fd, conn);
}

Connection* connection_find(int fd) {
    Connection* result = NULL;
    HASH_FIND_INT(connections, &fd, result);
    return result;
}

void connection_remove(int fd) {
    Connection* conn = connection_find(fd);
    if (conn) {
        HASH_DEL(connections, conn);
        connection_destroy(conn);
    }
}

void connection_close_all(void) {
    Connection* current;
    Connection* tmp;
    HASH_ITER(hh, connections, current, tmp) {
        HASH_DEL(connections, current);
        connection_destroy(current);
    }
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
        
        // Используем ваш buffer_protocol_set_expected для установки размера
        buffer_protocol_set_expected(&conn->read_buffer);
        
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

    // Отправляем все данные из буфера записи
    ssize_t n = write(conn->fd,
                      conn->write_buffer.data,
                      conn->write_buffer.position);

    if (n > 0) {
        // Сдвигаем оставшиеся данные в буфере
        uint32_t remaining = conn->write_buffer.position - n;
        if (remaining > 0) {
            memmove(conn->write_buffer.data, conn->write_buffer.data + n, remaining);
        }
        conn->write_buffer.position = remaining;

        // Если отправили все данные - очищаем буфер
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

    // Используем функции buffer.h для подготовки сообщения
    buffer_clear(&conn->write_buffer);
    buffer_reserve(&conn->write_buffer, (uint32_t)len);
    buffer_write(&conn->write_buffer, data, (uint32_t)len);

    int result = connection_write_data(conn);
    
    if (result == -2) { // EAGAIN/EWOULDBLOCK
        // Добавляем EPOLLOUT чтобы получить уведомление когда сокет снова готов к записи
        epoll_modify(g_epoll_fd, conn->fd, EPOLLIN | EPOLLOUT | EPOLLET);
    }
    
    return result;;
}

/* UDP */
void connection_set_udp_addr(Connection* conn, const struct sockaddr_in* udp_addr) {
    if (!conn || !udp_addr) return;
    memcpy(&conn->udp_addr, udp_addr, sizeof(struct sockaddr_in));
}

bool connection_has_udp(const Connection* conn) {
    return conn && conn->udp_addr.sin_port != 0;
}

/* Управление просматриваемыми трансляциями */
int connection_add_watch_stream(Connection* conn, Stream* stream) {
    if (!conn || !stream) return -1;
    if (connection_is_watching_stream(conn, stream)) return -2;
    if (connection_get_watch_stream_count(conn) >= MAX_INPUT) {
        return -3; // Нет места
    }
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
    
    int count = 0;
    for (int i = 0; i < MAX_INPUT; ++i) {
        if (conn->watch_streams[i] != NULL) count++;
    }
    return count;
}

/* Управление управляемыми трансляциями */
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
    
    int count = 0;
    for (int i = 0; i < MAX_OUTPUT; ++i) {
        if (conn->own_streams[i] != NULL) count++;
    }
    return count;
}

/* Поиск стрима в соединении */
Stream* connection_find_stream_by_id(const Connection* conn, uint32_t stream_id) {
    if (!conn) return NULL;
    
    // Сначала ищем в owned streams
    for (int i = 0; i < MAX_OUTPUT && conn->own_streams[i] != NULL; ++i) {
        if (conn->own_streams[i]->stream_id == stream_id) 
            return conn->own_streams[i];
    }
    
    // Затем в watched streams
    for (int i = 0; i < MAX_INPUT && conn->watch_streams[i] != NULL; ++i) {
        if (conn->watch_streams[i]->stream_id == stream_id) 
            return conn->watch_streams[i];
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
    
    // Добавляем watched streams
    for (int i = 0; i < MAX_INPUT && count < max_results; ++i) {
        if (conn->watch_streams[i] != NULL) {
            result[count++] = conn->watch_streams[i];
        }
    }
    
    // Добавляем owned streams
    for (int i = 0; i < MAX_OUTPUT && count < max_results; ++i) {
        if (conn->own_streams[i] != NULL) {
            result[count++] = conn->own_streams[i];
        }
    }
    
    return count;
}

/* Утилиты */
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
