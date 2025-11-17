#include "connection_logic.h"
#include "buffer_logic.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

void connection_detach_from_streams(Connection* conn) {
    if (!conn) return;

    // Создаем временный массив для стримов, от которых нужно отписаться
    Stream* streams_to_detach[MAX_INPUT];
    int count = 0;
    
    // Копируем указатели на стримы, чтобы избежать проблем при модификации массива
    for (int i = 0; i < MAX_INPUT; i++) {
        if (conn->watch_streams[i] != NULL) {
            streams_to_detach[count++] = conn->watch_streams[i];
        }
    }
    
    // Отписываем от всех стримов через stream_logic
    for (int i = 0; i < count; ++i) {
        if (streams_to_detach[i]) {
            stream_remove_recipient(streams_to_detach[i], conn);
        }
    }
    
    // ОЧИЩАЕМ массив watch_streams в соединении
    for (int i = 0; i < MAX_INPUT; i++) {
        conn->watch_streams[i] = NULL;
    }
}

void connection_delete_owned_streams(Connection* conn) {
    if (!conn) return;

    // Создаем временный массив для owned стримов
    Stream* owned_streams[MAX_OUTPUT];
    int count = 0;
    
    // Копируем указатели
    for (int i = 0; i < MAX_OUTPUT; i++) {
        if (conn->own_streams[i] != NULL) {
            owned_streams[count++] = conn->own_streams[i];
        }
    }
    
    // Удаляем все стримы через stream_logic
    for (int i = 0; i < count; ++i) {
        if (owned_streams[i]) {
            stream_destroy(owned_streams[i]);
        }
    }
    
    // ОЧИЩАЕМ массив own_streams в соединении
    for (int i = 0; i < MAX_OUTPUT; i++) {
        conn->own_streams[i] = NULL;
    }
}

void connection_close(Connection* conn) {
    if (!conn) return;
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
}

void connection_free(Connection* conn) {
    if (!conn) return;
    free(conn);
}

void connection_destroy_full(Connection* conn) {
    if (!conn) return;

    /* 1) Сначала удаляем owned streams через stream_logic */
    connection_delete_owned_streams(conn);

    /* 2) Затем удаляем conn как viewer из всех стримов через stream_logic */
    connection_detach_from_streams(conn);

    /* 3) Удаляем из глобальной хеш-таблицы */
    if (conn->fd >= 0) {
        Connection* found = NULL;
        HASH_FIND_INT(connections, &conn->fd, found);
        if (found == conn) {
            HASH_DEL(connections, conn);
        }
    }

    /* 4) Закрываем сокет */
    connection_close(conn);

    /* 5) Free */
    connection_free(conn);
}

/* Операции со стримами */
int connection_logic_add_own_stream(Connection* conn, Stream* stream) {
    return connection_add_own_stream(conn, stream);
}

int connection_logic_remove_own_stream(Connection* conn, Stream* stream) {
    return connection_remove_own_stream(conn, stream);
}

int connection_logic_add_watch_stream(Connection* conn, Stream* stream) {
    return connection_add_watch_stream(conn, stream);
}

int connection_logic_remove_watch_stream(Connection* conn, Stream* stream) {
    return connection_remove_watch_stream(conn, stream);
}