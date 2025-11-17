#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "connection.h"
#include "connection_logic.h"  // Добавлено для connection_detach_from_streams и connection_delete_owned_streams
#include "stream.h"
#include "stream_logic.h"  // Для stream_create, stream_add_recipient, stream_remove_recipient
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
            if (payload_len >= sizeof(UDPAddrFullPayload)) {
                handle_udp_addr(conn, (const UDPAddrFullPayload*) payload);
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

void handle_udp_addr(Connection* conn, const UDPAddrFullPayload* payload) {
    printf("handle_udp_addr: conn_fd=%d\n", conn->fd);
    
    // Проверяем что это IPv4
    if (ntohs(payload->family) != AF_INET) {
        printf("ERROR: Invalid address family: %u\n", ntohs(payload->family));
        return;
    }
    
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = payload->port; 
    udp_addr.sin_addr.s_addr = payload->ip;
    
    connection_set_udp_addr(conn, &udp_addr);
    
    char addr_str[64];
    sockaddr_to_string(&udp_addr, addr_str, sizeof(addr_str));
    printf("Client %s set UDP address: %s\n", 
           connection_get_address_string(conn), addr_str);
}

void handle_disconnect(Connection* conn) {
    printf("handle_disconnect: conn_fd=%d\n", conn->fd);
    // Заменяем cleanup_streams_on_disconnect на правильные функции
    connection_detach_from_streams(conn);    // Отписываем от всех стримов как зритель
    connection_delete_owned_streams(conn);   // Удаляем все стримы как владелец
}

void handle_stream_create(Connection* conn) {
    printf("handle_stream_create: conn_fd=%d\n", conn->fd);
    
    // Заменяем create_stream_for_connection на stream_create
    Stream* stream = stream_create(0, conn); // 0 - сгенерируется автоматически
    
    if (stream != NULL) {
        send_stream_created(conn, stream);
    } else {
        printf("ERROR: Failed to create stream for connection %d\n", conn->fd);
    }
}

void handle_stream_delete(Connection* conn, const StreamIDPayload* payload) {
    printf("handle_stream_delete: conn_fd=%d, ", conn->fd);
    print_stream_id(ntohl(payload->stream_id));
    printf("\n");
    
    Stream* stream = stream_find_by_id(ntohl(payload->stream_id));
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
    
    // Удаляем трансляцию через stream_logic (автоматически удаляет из реестра)
    stream_destroy(stream);
}

void handle_stream_join(Connection* conn, const StreamIDPayload* payload) {
    printf("handle_stream_join: conn_fd=%d, ", conn->fd);
    print_stream_id(ntohl(payload->stream_id));
    printf("\n");
    
    Stream* stream = stream_find_by_id(ntohl(payload->stream_id));
    if (!stream) {
        printf("ERROR: Stream not found\n");
        send_join_result(conn, NULL, -1);
        return;
    }
    
    // Проверяем, не является ли уже получателем
    if (stream_is_recipient_in_array(stream, conn)) {
        printf("ERROR: Already recipient of this stream\n");
        send_join_result(conn, stream, -2);
        return;
    }
    
    // Заменяем add_recipient_to_stream на stream_add_recipient
    int result = stream_add_recipient(stream, conn);
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
    print_stream_id(ntohl(payload->stream_id));
    printf("\n");
    
    Stream* stream = stream_find_by_id(ntohl(payload->stream_id));
    if (!stream) {
        printf("ERROR: Stream not found\n");
        return;
    }
    
    // Заменяем remove_recipient_from_stream на stream_remove_recipient
    int result = stream_remove_recipient(stream, conn);
    
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
    stream_id = ntohl(stream_id);
    printf(", stream_id=%u", stream_id);
    
    // Извлекаем номер пакета
    uint32_t packet_number;
    memcpy(&packet_number, data + 4, 4);
    packet_number = ntohl(packet_number);
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
    payload.stream_id = htonl(stream->stream_id);
    
    uint8_t message[5];
    message[0] = SERVER_STREAM_CREATED;
    memcpy(&message[1], &payload, sizeof(payload));
    
    printf("DEBUG: Sending SERVER_STREAM_CREATED: type=0x%02x, stream_id=%u\n",
           message[0], ntohl(payload.stream_id));
    
    // Отправляем сообщение
    connection_send_message(conn, message, sizeof(message));
    
    // НЕМЕДЛЕННАЯ отправка данных
    printf("DEBUG: Immediate flush for fd=%d\n", conn->fd);
    connection_write_data(conn);
    
    // Принудительно добавляем EPOLLOUT если есть данные
    if (conn->write_buffer.position > 0) {
        printf("DEBUG: Still data in buffer, adding EPOLLOUT for fd=%d\n", conn->fd);
        epoll_modify(g_epoll_fd, conn->fd, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

void send_stream_deleted_to_recipients(const Stream* stream) {
    printf("send_stream_deleted_to_recipients: ");
    print_stream_id(stream->stream_id);
    printf(", recipient_count=%u\n", stream->recipient_count);
    
    StreamIDPayload payload;
    payload.stream_id = htonl(stream->stream_id);  // Исправлено: добавляем htonl
    
    broadcast_to_stream(stream, SERVER_STREAM_DELETED, &payload, sizeof(payload), NULL);
}

void send_join_result(Connection* conn, const Stream* stream, int result) {
    printf("send_join_result: to conn_fd=%d, result=%d\n", conn->fd, result);
    
    if (stream && result == 0) {
        // Успешное присоединение
        StreamIDPayload payload;
        payload.stream_id = htonl(stream->stream_id);  // Исправлено: добавляем htonl
        
        uint8_t message[5];
        message[0] = SERVER_STREAM_JOINED;
        memcpy(&message[1], &payload, sizeof(payload));
        connection_send_message(conn, message, sizeof(message));
    } else {
        // Ошибка присоединения - отправляем сообщение без payload
        uint8_t message[1] = {SERVER_STREAM_JOINED};
        connection_send_message(conn, message, 1);
    }
}

void send_stream_start_to_owner(const Stream* stream) {
    printf("send_stream_start_to_owner: ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    StreamIDPayload payload;
    payload.stream_id = htonl(stream->stream_id);  // Исправлено: добавляем htonl
    
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
    payload.stream_id = htonl(stream->stream_id);  // Исправлено: добавляем htonl
    
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
    payload.stream_id = htonl(stream->stream_id);  // Исправлено: добавляем htonl
    payload.recipient_addr.port = new_recipient->udp_addr.sin_port;
    payload.recipient_addr.ip = new_recipient->udp_addr.sin_addr.s_addr;
    
    broadcast_to_stream(stream, SERVER_NEW_RECIPIENT, &payload, sizeof(payload), new_recipient);
}

void send_recipient_left_to_stream(const Stream* stream, const Connection* left_recipient) {
    printf("send_recipient_left_to_stream: ");
    print_stream_id(stream->stream_id);
    printf(", left_recipient_fd=%d\n", left_recipient->fd);
    
    RecipientNotificationPayload payload;
    payload.stream_id = htonl(stream->stream_id);  // Исправлено: добавляем htonl
    payload.recipient_addr.port = left_recipient->udp_addr.sin_port;
    payload.recipient_addr.ip = left_recipient->udp_addr.sin_addr.s_addr;
    
    broadcast_to_stream(stream, SERVER_RECIPIENT_LEFT, &payload, sizeof(payload), left_recipient);
}

// ==================== СЛУЖЕБНЫЕ ФУНКЦИИ ====================

void handle_connection_closed(Connection* conn) {
    printf("handle_connection_closed: conn_fd=%d\n", conn->fd);
    // Заменяем cleanup_streams_on_disconnect на правильные функции
    connection_detach_from_streams(conn);    // Отписываем от всех стримов как зритель
    connection_delete_owned_streams(conn);   // Удаляем все стримы как владелец
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