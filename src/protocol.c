#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "connection.h"
#include "handlers.h"
#include "stream.h"
#include "network.h"

// Глобальная переменная для UDP сокета
extern int g_udp_fd;

// Вспомогательная функция для печати UDP адреса
static void print_udp_addr(const UDPAddrPayload* addr) {
    struct in_addr ip_addr;
    ip_addr.s_addr = addr->ip;
    printf("UDP %s:%d", inet_ntoa(ip_addr), ntohs(addr->port));
}

// Вспомогательная функция для печати ID трансляции
static void print_stream_id(uint32_t stream_id) {
    printf("Stream %u", stream_id);
}

// ==================== ОБРАБОТЧИКИ TCP СООБЩЕНИЙ ОТ КЛИЕНТА ====================

void handle_client_message(Connection* conn, uint8_t message_type, const uint8_t* payload, size_t payload_len) {
    printf("handle_client_message: conn_fd=%d, type=0x%02x, payload_len=%zu\n", 
           conn->fd, message_type, payload_len);
    
    switch (message_type) {
        case CLIENT_UDP_ADDR:
            if (payload_len >= sizeof(UDPAddrPayload)) {
                handle_udp_addr(conn, (const UDPAddrPayload*)payload);
            } else {
                printf("ERROR: Invalid UDP addr payload size\n");
            }
            break;
        case CLIENT_DISCONNECT:
            handle_disconnect(conn);
            break;
        case CLIENT_STREAM_CREATE:
            handle_stream_create(conn);
            break;
        case CLIENT_STREAM_DELETE:
            if (payload_len >= sizeof(StreamIDPayload)) {
                handle_stream_delete(conn, (const StreamIDPayload*)payload);
            } else {
                printf("ERROR: Invalid stream delete payload size\n");
            }
            break;
        case CLIENT_STREAM_JOIN:
            if (payload_len >= sizeof(StreamIDPayload)) {
                handle_stream_join(conn, (const StreamIDPayload*)payload);
            } else {
                printf("ERROR: Invalid stream join payload size\n");
            }
            break;
        case CLIENT_STREAM_LEAVE:
            if (payload_len >= sizeof(StreamIDPayload)) {
                handle_stream_leave(conn, (const StreamIDPayload*)payload);
            } else {
                printf("ERROR: Invalid stream leave payload size\n");
            }
            break;
        default:
            printf("ERROR: Unknown message type 0x%02x\n", message_type);
            break;
    }
}

void handle_udp_addr(Connection* conn, const UDPAddrPayload* payload) {
    printf("handle_udp_addr: conn_fd=%d, ", conn->fd);
    print_udp_addr(payload);
    printf("\n");
    
    // Сохраняем UDP адрес клиента
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = payload->port;
    udp_addr.sin_addr.s_addr = payload->ip;
    
    connection_set_udp_addr(conn, &udp_addr);
}

void handle_disconnect(Connection* conn) {
    printf("handle_disconnect: conn_fd=%d\n", conn->fd);
    cleanup_streams_on_disconnect(conn);
}

void handle_stream_create(Connection* conn) {
    printf("handle_stream_create: conn_fd=%d\n", conn->fd);
    
    // Генерируем ID трансляции и создаем ее
    uint32_t stream_id = stream_generate_id();
    int result = create_stream_for_connection(conn, stream_id);
    
    if (result == 0) {
        send_stream_created(conn, stream_find_by_id(stream_id));
    }
}

void handle_stream_delete(Connection* conn, const StreamIDPayload* payload) {
    printf("handle_stream_delete: conn_fd=%d, ", conn->fd);
    print_stream_id(payload->stream_id);
    printf("\n");
    
    Stream* stream = stream_find_by_id(payload->stream_id);
    if (!stream) {
        printf("ERROR: Stream not found\n");
        return;
    }
    
    // Проверяем, что соединение является владельцем трансляции
    if (stream->owner != conn) {
        printf("ERROR: Connection is not stream owner\n");
        return;
    }
    
    // Уведомляем всех получателей о закрытии трансляции
    send_stream_deleted_to_recipients(stream);
    
    // Удаляем трансляцию
    stream_remove_from_registry(stream);
    stream_destroy(stream);
}

void handle_stream_join(Connection* conn, const StreamIDPayload* payload) {
    printf("handle_stream_join: conn_fd=%d, ", conn->fd);
    print_stream_id(payload->stream_id);
    printf("\n");
    
    Stream* stream = stream_find_by_id(payload->stream_id);
    if (!stream) {
        printf("ERROR: Stream not found\n");
        send_join_result(conn, NULL, -1);
        return;
    }
    
    // Проверяем, не является ли клиент уже получателем
    if (stream_is_recipient(stream, conn)) {
        printf("ERROR: Already recipient of this stream\n");
        send_join_result(conn, stream, -2);
        return;
    }
    
    // Добавляем получателя в трансляцию
    int result = add_recipient_to_stream(stream, conn);
    send_join_result(conn, stream, result);
    
    if (result == 0) {
        // Уведомляем владельца о новом получателе
        if (stream->recipient_count == 1) {
            send_stream_start_to_owner(stream);
        }
        
        // Уведомляем других получателей о новом участнике
        send_new_recipient_to_stream(stream, conn);
    }
}

void handle_stream_leave(Connection* conn, const StreamIDPayload* payload) {
    printf("handle_stream_leave: conn_fd=%d, ", conn->fd);
    print_stream_id(payload->stream_id);
    printf("\n");
    
    Stream* stream = stream_find_by_id(payload->stream_id);
    if (!stream) {
        printf("ERROR: Stream not found\n");
        return;
    }
    
    // Удаляем получателя из трансляции
    int result = remove_recipient_from_stream(stream, conn);
    
    if (result == 0) {
        // Уведомляем других получателей о выходе участника
        send_recipient_left_to_stream(stream, conn);
        
        // Уведомляем владельца если не осталось получателей
        if (stream->recipient_count == 0) {
            send_stream_end_to_owner(stream);
        }
    }
}

// ==================== ОБРАБОТЧИКИ UDP ПАКЕТОВ ====================

void handle_udp_packet(const uint8_t* data, size_t len, const struct sockaddr_in* src_addr) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src_addr->sin_addr, ip_str, sizeof(ip_str));
    
    printf("handle_udp_packet: from %s:%d, len=%zu", 
           ip_str, ntohs(src_addr->sin_port), len);
    
    // Проверяем минимальный размер пакета (8 байт: 4 байта ID + 4 байта номера пакета)
    if (len < 8) {
        printf("ERROR: UDP packet too small: %zu bytes\n", len);
        return;
    }
    
    // Извлекаем ID трансляции
    uint32_t stream_id;
    memcpy(&stream_id, data, 4);
    printf(", stream_id=%u", stream_id);
    
    // Извлекаем номер пакета
    uint32_t packet_number;
    memcpy(&packet_number, data + 4, 4);
    printf(", packet_number=%u", packet_number);
    printf("\n");
    
    // Находим трансляцию
    Stream* stream = stream_find_by_id(stream_id);
    if (!stream) {
        printf("ERROR: Stream %u not found\n", stream_id);
        return;
    }
    
    // Пересылаем пакет всем получателям
    for (uint32_t i = 0; i < stream->recipient_count; i++) {
        Connection* recipient = stream->recipients[i];
        if (connection_has_udp(recipient)) {
            // Используем глобальный UDP сокет
            udp_send_packet(g_udp_fd, data, len, &recipient->udp_addr);
        }
    }
}

// ==================== ФУНКЦИИ ОТПРАВКИ ОТВЕТОВ КЛИЕНТАМ ====================

void send_stream_created(Connection* conn, const Stream* stream) {
    printf("send_stream_created: to conn_fd=%d, ", conn->fd);
    print_stream_id(stream->stream_id);
    printf("\n");
    
    StreamIDPayload payload;
    payload.stream_id = stream->stream_id;
    
    uint8_t message[5];
    message[0] = SERVER_STREAM_CREATED;
    memcpy(&message[1], &payload, sizeof(payload));
    
    connection_send_message(conn, message, sizeof(message));
}

void send_stream_deleted_to_recipients(const Stream* stream) {
    printf("send_stream_deleted_to_recipients: ");
    print_stream_id(stream->stream_id);
    printf(", recipient_count=%u\n", stream->recipient_count);
    
    StreamIDPayload payload;
    payload.stream_id = stream->stream_id;
    
    broadcast_to_stream(stream, SERVER_STREAM_DELETED, &payload, sizeof(payload), NULL);
}

void send_join_result(Connection* conn, const Stream* stream, int result) {
    printf("send_join_result: to conn_fd=%d, result=%d\n", conn->fd, result);
    
    if (stream && result == 0) {
        // Успешное присоединение
        StreamIDPayload payload;
        payload.stream_id = stream->stream_id;
        
        uint8_t message[5];
        message[0] = SERVER_STREAM_JOINED;
        memcpy(&message[1], &payload, sizeof(payload));
        connection_send_message(conn, message, sizeof(message));
    } else {
        // Ошибка присоединения
        uint8_t message[1] = {SERVER_STREAM_JOINED};
        connection_send_message(conn, message, 1);
    }
}

void send_stream_start_to_owner(const Stream* stream) {
    printf("send_stream_start_to_owner: ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    StreamIDPayload payload;
    payload.stream_id = stream->stream_id;
    
    uint8_t message[5];
    message[0] = SERVER_STREAM_START;
    memcpy(&message[1], &payload, sizeof(payload));
    
    connection_send_message(stream->owner, message, sizeof(message));
}

void send_stream_end_to_owner(const Stream* stream) {
    printf("send_stream_end_to_owner: ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    StreamIDPayload payload;
    payload.stream_id = stream->stream_id;
    
    uint8_t message[5];
    message[0] = SERVER_STREAM_END;
    memcpy(&message[1], &payload, sizeof(payload));
    
    connection_send_message(stream->owner, message, sizeof(message));
}

void send_new_recipient_to_stream(const Stream* stream, const Connection* new_recipient) {
    printf("send_new_recipient_to_stream: ");
    print_stream_id(stream->stream_id);
    printf(", new_recipient_fd=%d\n", new_recipient->fd);
    
    RecipientNotificationPayload payload;
    payload.stream_id = stream->stream_id;
    payload.recipient_addr.port = new_recipient->udp_addr.sin_port;
    payload.recipient_addr.ip = new_recipient->udp_addr.sin_addr.s_addr;
    
    broadcast_to_stream(stream, SERVER_NEW_RECIPIENT, &payload, sizeof(payload), new_recipient);
}

void send_recipient_left_to_stream(const Stream* stream, const Connection* left_recipient) {
    printf("send_recipient_left_to_stream: ");
    print_stream_id(stream->stream_id);
    printf(", left_recipient_fd=%d\n", left_recipient->fd);
    
    RecipientNotificationPayload payload;
    payload.stream_id = stream->stream_id;
    payload.recipient_addr.port = left_recipient->udp_addr.sin_port;
    payload.recipient_addr.ip = left_recipient->udp_addr.sin_addr.s_addr;
    
    broadcast_to_stream(stream, SERVER_RECIPIENT_LEFT, &payload, sizeof(payload), left_recipient);
}

// ==================== СЛУЖЕБНЫЕ ФУНКЦИИ ====================

void handle_connection_closed(Connection* conn) {
    printf("handle_connection_closed: conn_fd=%d\n", conn->fd);
    cleanup_streams_on_disconnect(conn);
}

void broadcast_to_stream(const Stream* stream, uint8_t message_type, const void* payload, size_t payload_len, const Connection* exclude) {
    printf("broadcast_to_stream: ");
    print_stream_id(stream->stream_id);
    printf(", type=0x%02x, payload_len=%zu, exclude_fd=%d\n", 
           message_type, payload_len, exclude ? exclude->fd : -1);
    
    // Создаем полное сообщение
    uint8_t* message = malloc(1 + payload_len);
    if (!message) return;
    
    message[0] = message_type;
    memcpy(&message[1], payload, payload_len);
    
    // Отправляем всем получателям кроме исключенного
    for (uint32_t i = 0; i < stream->recipient_count; i++) {
        Connection* recipient = stream->recipients[i];
        if (recipient != exclude) {
            connection_send_message(recipient, message, 1 + payload_len);
        }
    }
    
    free(message);
}