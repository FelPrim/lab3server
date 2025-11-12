#include "handlers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "connection.h"
#include "stream.h"
#include "protocol.h"

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ДЛЯ ТРАНСЛЯЦИЙ ====================

int create_stream_for_connection(Connection* conn, uint32_t stream_id) {
    printf("create_stream_for_connection: creating stream id=%u for conn_fd=%d\n", 
           stream_id, conn->fd);
    
    // Проверяем, не превышен ли лимит управляемых трансляций
    if (connection_get_own_stream_count(conn) >= MAX_OUTPUT) {
        printf("ERROR: Maximum output streams reached\n");
        return -1;
    }
    
    // Создаем трансляцию
    Stream* stream = stream_create(stream_id, conn);
    if (!stream) {
        printf("ERROR: Failed to create stream\n");
        return -1;
    }
    
    // Добавляем в глобальный реестр
    stream_add_to_registry(stream);
    
    // Добавляем в список управляемых трансляций соединения
    int result = connection_add_own_stream(conn, stream);
    if (result != 0) {
        printf("ERROR: Failed to add stream to connection\n");
        stream_remove_from_registry(stream);
        stream_destroy(stream);
        return -1;
    }
    
    return 0;
}

int add_recipient_to_stream(Stream* stream, Connection* recipient) {
    printf("add_recipient_to_stream: adding recipient fd=%d to stream id=%u\n", 
           recipient->fd, stream->stream_id);
    
    // Проверяем, не превышен ли лимит просматриваемых трансляций
    if (connection_get_watch_stream_count(recipient) >= MAX_INPUT) {
        printf("ERROR: Maximum input streams reached\n");
        return -1;
    }
    
    // Проверяем UDP адрес получателя
    if (!connection_has_udp(recipient)) {
        printf("ERROR: Recipient has no UDP address\n");
        return -2;
    }
    
    // Добавляем получателя в трансляцию
    int result = stream_add_recipient(stream, recipient);
    if (result != 0) {
        printf("ERROR: Failed to add recipient to stream: %d\n", result);
        return result;
    }
    
    // Добавляем трансляцию в список просматриваемых получателя
    result = connection_add_watch_stream(recipient, stream);
    if (result != 0) {
        printf("ERROR: Failed to add stream to recipient: %d\n", result);
        stream_remove_recipient(stream, recipient);
        return -1;
    }
    
    return 0;
}

int remove_recipient_from_stream(Stream* stream, Connection* recipient) {
    printf("remove_recipient_from_stream: removing recipient fd=%d from stream id=%u\n", 
           recipient->fd, stream->stream_id);
    
    // Удаляем получателя из трансляции
    int result = stream_remove_recipient(stream, recipient);
    if (result != 0) {
        printf("ERROR: Failed to remove recipient from stream: %d\n", result);
        return result;
    }
    
    // Удаляем трансляцию из списка просматриваемых получателя
    result = connection_remove_watch_stream(recipient, stream);
    if (result != 0) {
        printf("ERROR: Failed to remove stream from recipient: %d\n", result);
        // Продолжаем, так как получатель уже удален из трансляции
    }
    
    return 0;
}

void cleanup_streams_on_disconnect(Connection* conn) {
    printf("cleanup_streams_on_disconnect: cleaning up streams for conn_fd=%d\n", conn->fd);
    
    // Удаляем соединение из всех трансляций, где оно является получателем
    Stream* watch_streams[MAX_INPUT];
    int watch_count = connection_get_all_streams(conn, watch_streams, MAX_INPUT);
    
    for (int i = 0; i < watch_count; i++) {
        Stream* stream = watch_streams[i];
        if (stream && stream_is_recipient(stream, conn)) {
            remove_recipient_from_stream(stream, conn);
            
            // Уведомляем владельца если не осталось получателей
            if (stream->recipient_count == 0) {
                send_stream_end_to_owner(stream);
            }
        }
    }
    
    // Закрываем все управляемые трансляции
    Stream* own_streams[MAX_OUTPUT];
    int own_count = stream_find_by_owner(conn, own_streams, MAX_OUTPUT);
    
    for (int i = 0; i < own_count; i++) {
        Stream* stream = own_streams[i];
        if (stream) {
            // Уведомляем всех получателей о закрытии трансляции
            send_stream_deleted_to_recipients(stream);
            
            // Удаляем трансляцию
            stream_remove_from_registry(stream);
            stream_destroy(stream);
        }
    }
}