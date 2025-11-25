#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "connection.h"
#include "connection.h"
#include "stream.h"
#include "stream.h"
#include "call.h"
#include "call.h"
#include "network.h"
#include "id_utils.h"

// Глобальные переменные
extern int g_udp_fd;
extern int g_epoll_fd;

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

static void print_stream_id(uint32_t stream_id) {
    char str[7];
    id_to_string(stream_id, str);
    str[6] = '\0';
    printf("Stream %s(%u)", str, stream_id);
}

static void print_call_id(uint32_t call_id) {
    char str[7];
    id_to_string(call_id, str);
    str[6] = '\0';
    printf("Call %s(%u)", str, call_id);
}

static void print_connection_id(const Connection* conn) {
    printf("Connection %d", conn->fd);
}

// Функция для отправки сообщений об ошибке
static void send_error(Connection* conn, uint8_t original_message, const char* error_msg) {
    printf("Sending error: %s\n", error_msg);
    
    size_t msg_len = strlen(error_msg);
    ErrorSuccessPayload payload;
    payload.original_message_type = original_message;
    payload.message_length = (uint8_t)msg_len;
    
    // Выделяем память для полного сообщения
    size_t total_len = 1 + sizeof(ErrorSuccessPayload) + msg_len;
    uint8_t* message = malloc(total_len);
    if (!message) return;
    
    message[0] = SERVER_ERROR;
    memcpy(message + 1, &payload, sizeof(payload));
    memcpy(message + 1 + sizeof(payload), error_msg, msg_len);
    
    connection_send_message(conn, message, total_len);
    free(message);
}

// Функция для отправки сообщений об успехе
static void send_success(Connection* conn, uint8_t original_message, const char* success_msg) {
    printf("Sending success: %s\n", success_msg);
    
    size_t msg_len = strlen(success_msg);
    ErrorSuccessPayload payload;
    payload.original_message_type = original_message;
    payload.message_length = (uint8_t)msg_len;
    
    size_t total_len = 1 + sizeof(ErrorSuccessPayload) + msg_len;
    uint8_t* message = malloc(total_len);
    if (!message) return;
    
    message[0] = SERVER_SUCCESS;
    memcpy(message + 1, &payload, sizeof(payload));
    memcpy(message + 1 + sizeof(payload), success_msg, msg_len);
    
    connection_send_message(conn, message, total_len);
    free(message);
}

// ==================== ОБРАБОТЧИКИ БАЗОВЫХ СООБЩЕНИЙ ====================

// ИЗМЕНИТЬ: убрать неиспользуемый параметр conn
void handle_client_error(const ErrorSuccessPayload* payload) {
    char message[256];
    if (payload->message_length > 0) {
        size_t copy_len = payload->message_length;
        if (copy_len >= sizeof(message)) {
            copy_len = sizeof(message) - 1;
        }
        memcpy(message, (const uint8_t*)payload + sizeof(ErrorSuccessPayload), copy_len);
        message[copy_len] = '\0';
        printf("Client error (original msg 0x%02x): %s\n", 
               payload->original_message_type, message);
    }
}

// ИЗМЕНИТЬ: убрать неиспользуемый параметр conn
void handle_client_success(const ErrorSuccessPayload* payload) {
    char message[256];
    if (payload->message_length > 0) {
        size_t copy_len = payload->message_length;
        if (copy_len >= sizeof(message)) {
            copy_len = sizeof(message) - 1;
        }
        memcpy(message, (const uint8_t*)payload + sizeof(ErrorSuccessPayload), copy_len);
        message[copy_len] = '\0';
        printf("Client success (original msg 0x%02x): %s\n", 
               payload->original_message_type, message);
    }
}


// ==================== ОБРАБОТЧИКИ СТРИМОВ ====================

void handle_stream_create(Connection* conn, const StreamCreatePayload* payload) {
    printf("handle_stream_create: ");
    print_connection_id(conn);
    printf(", call_id=%u\n", ntohl(payload->call_id));
    
    uint32_t call_id = ntohl(payload->call_id);
    Call* call = NULL;
    
    // Проверяем, приватный ли это стрим
    if (call_id != 0) {
        call = call_find_by_id(call_id);
        if (!call) {
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "ERROR: COULDN'T FIND CALL WITH ID %u", call_id);
            send_error(conn, CLIENT_STREAM_CREATE, error_msg);
            return;
        }
        
        // Проверяем, что соединение является участником звонка
        if (!call_has_participant(call, conn)) {
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "ERROR: %u ISN'T A PARTICIPANT OF THE CALL %u", 
                     conn->fd, call_id);
            send_error(conn, CLIENT_STREAM_CREATE, error_msg);
            return;
        }
    }
    
    // Создаем стрим
    Stream* stream = stream_new(0, conn, call);
    if (!stream) {
        fprintf(stderr, "Failed to create stream in handle_stream_create\n");
        send_error(conn, CLIENT_STREAM_CREATE, "ERROR: FAILED TO CREATE STREAM");
        return;
    }
    
    // Отправляем ответ
    send_stream_created(conn, stream);
    
    // Если стрим приватный, уведомляем участников звонка
    if (call) {
        send_call_stream_new(call, stream);
    }
}


void handle_stream_delete(Connection* conn, const StreamIDPayload* payload) {
    uint32_t stream_id = ntohl(payload->stream_id);
    printf("handle_stream_delete: ");
    print_connection_id(conn);
    printf(", ");
    print_stream_id(stream_id);
    printf("\n");
    
    Stream* stream = stream_find_by_id(stream_id);
    if (!stream) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: COULDN'T FIND STREAM WITH ID %u", stream_id);
        send_error(conn, CLIENT_STREAM_DELETE, error_msg);
        return;
    }
    
    // Проверяем, что соединение является владельцем стрима
    if (stream->owner != conn) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: %u ISN'T AN OWNER OF THE STREAM %u", 
                 conn->fd, stream_id);
        send_error(conn, CLIENT_STREAM_DELETE, error_msg);
        return;
    }
    
    Call* call = stream->call;
    
    // Уведомляем зрителей о удалении стрима
    send_stream_deleted(stream);
    
    // Если стрим приватный, уведомляем участников звонка
    if (call) {
        send_call_stream_deleted(call, stream);
    }
    
    // Удаляем стрим
    stream_delete(stream);
    
    // Отправляем подтверждение
    char success_msg[64];
    snprintf(success_msg, sizeof(success_msg), "SUCCESS: deleted stream %u", stream_id);
    send_success(conn, CLIENT_STREAM_DELETE, success_msg);
}

void handle_stream_join(Connection* conn, const StreamIDPayload* payload) {
    uint32_t stream_id = ntohl(payload->stream_id);
    printf("handle_stream_join: ");
    print_connection_id(conn);
    printf(", ");
    print_stream_id(stream_id);
    printf("\n");
    
    Stream* stream = stream_find_by_id(stream_id);
    if (!stream) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: COULDN'T FIND STREAM WITH ID %u", stream_id);
        send_error(conn, CLIENT_STREAM_CONN_JOIN, error_msg);
        return;
    }
    
    // Для приватных стримов проверяем, что соединение в том же звонке
    if (stream->call) {
        if (!call_has_participant(stream->call, conn)) {
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "ERROR: NOT IN THE SAME CALL AS STREAM %u", stream_id);
            send_error(conn, CLIENT_STREAM_CONN_JOIN, error_msg);
            return;
        }
    }
    
    // Добавляем получателя
    int result = stream_add_recipient(stream, conn);
    if (result != 0) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: FAILED TO JOIN STREAM %u (code %d)", stream_id, result);
        send_error(conn, CLIENT_STREAM_CONN_JOIN, error_msg);
        return;
    }
    
    // Отправляем подтверждение
    send_stream_joined(conn, stream);
    
    // Если это первый зритель, уведомляем владельца
    if (stream_get_recipient_count(stream) == 1) {
        send_stream_start(stream);
    }
}

void handle_stream_leave(Connection* conn, const StreamIDPayload* payload) {
    uint32_t stream_id = ntohl(payload->stream_id);
    printf("handle_stream_leave: ");
    print_connection_id(conn);
    printf(", ");
    print_stream_id(stream_id);
    printf("\n");
    
    Stream* stream = stream_find_by_id(stream_id);
    if (!stream) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: COULDN'T FIND STREAM WITH ID %u", stream_id);
        send_error(conn, CLIENT_STREAM_CONN_LEAVE, error_msg);
        return;
    }
    
    // Удаляем получателя
    int result = stream_remove_recipient(stream, conn);
    if (result != 0) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: FAILED TO LEAVE STREAM %u (code %d)", stream_id, result);
        send_error(conn, CLIENT_STREAM_CONN_LEAVE, error_msg);
        return;
    }
    
    // Если не осталось зрителей, уведомляем владельца
    if (stream_get_recipient_count(stream) == 0) {
        send_stream_end(stream);
    }
    
    // Отправляем подтверждение
    char success_msg[64];
    snprintf(success_msg, sizeof(success_msg), "SUCCESS: left stream %u", stream_id);
    send_success(conn, CLIENT_STREAM_CONN_LEAVE, success_msg);
}

// ==================== ОБРАБОТЧИКИ ЗВОНКОВ ====================

void handle_call_create(Connection* conn) {
    printf("handle_call_create: ");
    print_connection_id(conn);
    printf("\n");
    
    Call* call = call_new(0);
    if (!call) {
        send_error(conn, CLIENT_CALL_CREATE, "ERROR: FAILED TO CREATE CALL");
        return;
    }
    
    // Добавляем создателя как участника
    if (call_add_participant(call, conn) != 0) {
        call_delete(call);
        send_error(conn, CLIENT_CALL_CREATE, "ERROR: FAILED TO ADD CREATOR TO CALL");
        return;
    }
    
    // Отправляем ответ
    send_call_created(conn, call);
}

void handle_call_join(Connection* conn, const CallJoinPayload* payload) {
    uint32_t call_id = ntohl(payload->call_id);
    printf("handle_call_join: ");
    print_connection_id(conn);
    printf(", ");
    print_call_id(call_id);
    printf("\n");
    
    Call* call = call_find_by_id(call_id);
    if (!call) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: COULDN'T FIND CALL WITH ID %u", call_id);
        send_error(conn, CLIENT_CALL_CONN_JOIN, error_msg);
        return;
    }
    
    // Добавляем участника
    int result = call_add_participant(call, conn);
    if (result != 0) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: FAILED TO JOIN CALL %u (code %d)", call_id, result);
        send_error(conn, CLIENT_CALL_CONN_JOIN, error_msg);
        return;
    }
    
    // Отправляем ответ новому участнику
    send_call_joined(conn, call);
    
    // Уведомляем других участников о новом участнике
    send_call_conn_new(call, conn);
}

void handle_call_leave(Connection* conn, const CallJoinPayload* payload) {
    uint32_t call_id = ntohl(payload->call_id);
    printf("handle_call_leave: ");
    print_connection_id(conn);
    printf(", ");
    print_call_id(call_id);
    printf("\n");
    
    Call* call = call_find_by_id(call_id);
    if (!call) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: COULDN'T FIND CALL WITH ID %u", call_id);
        send_error(conn, CLIENT_CALL_CONN_LEAVE, error_msg);
        return;
    }
    
    // Проверяем, что соединение является участником звонка
    if (!call_has_participant(call, conn)) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "ERROR: %u ISN'T A PARTICIPANT OF THE CALL %u", 
                 conn->fd, call_id);
        send_error(conn, CLIENT_CALL_CONN_LEAVE, error_msg);
        return;
    }
    
    // Получаем все стримы, которыми владеет участник в этом звонке
    Stream* owned_streams[MAX_CALL_STREAMS];
    int owned_count = 0;
    for (int i = 0; i < MAX_CALL_STREAMS; i++) {
        if (call->streams[i] && call->streams[i]->owner == conn) {
            owned_streams[owned_count++] = call->streams[i];
        }
    }
    
    // Удаляем участника из звонка
    if (call_remove_participant(call, conn) != 0) {
        send_error(conn, CLIENT_CALL_CONN_LEAVE, "ERROR: FAILED TO REMOVE FROM CALL");
        return;
    }
    
    // Удаляем стримы, которыми владел участник
    for (int i = 0; i < owned_count; i++) {
        send_stream_deleted(owned_streams[i]);
        send_call_stream_deleted(call, owned_streams[i]);
        stream_delete(owned_streams[i]);
    }
    
    // Уведомляем других участников
    send_call_conn_left(call, conn);
    
    // Если звонок пуст, удаляем его
    if (call_get_participant_count(call) == 0) {
        call_delete(call);
    }
    
    // Отправляем подтверждение
    char success_msg[64];
    snprintf(success_msg, sizeof(success_msg), "SUCCESS: left call %u", call_id);
    send_success(conn, CLIENT_CALL_CONN_LEAVE, success_msg);
}

// ==================== ГЛАВНЫЙ ДИСПЕТЧЕР СООБЩЕНИЙ ====================

void handle_client_message(Connection* conn, uint8_t message_type, const uint8_t* payload, size_t payload_len) {
    printf("handle_client_message: conn_fd=%d, type=0x%02x, payload_len=%zu\n", 
           conn->fd, message_type, payload_len);
    
    switch (message_type) {
        // Базовые сообщения
        case CLIENT_ERROR:
            if (payload_len >= sizeof(ErrorSuccessPayload)) {
                handle_client_error((const ErrorSuccessPayload*)payload);
            }
            break;
        case CLIENT_SUCCESS:
            if (payload_len >= sizeof(ErrorSuccessPayload)) {
                handle_client_success((const ErrorSuccessPayload*)payload);
            }
            break;
            
        // Сообщения стримов
        case CLIENT_STREAM_CREATE:
            if (payload_len >= sizeof(StreamCreatePayload)) {
                handle_stream_create(conn, (const StreamCreatePayload*)payload);
            }
            break;
        case CLIENT_STREAM_DELETE:
            if (payload_len >= sizeof(StreamIDPayload)) {
                handle_stream_delete(conn, (const StreamIDPayload*)payload);
            }
            break;
        case CLIENT_STREAM_CONN_JOIN:
            if (payload_len >= sizeof(StreamIDPayload)) {
                handle_stream_join(conn, (const StreamIDPayload*)payload);
            }
            break;
        case CLIENT_STREAM_CONN_LEAVE:
            if (payload_len >= sizeof(StreamIDPayload)) {
                handle_stream_leave(conn, (const StreamIDPayload*)payload);
            }
            break;
            
        // Сообщения звонков
        case CLIENT_CALL_CREATE:
            handle_call_create(conn);
            break;
        case CLIENT_CALL_CONN_JOIN:
            if (payload_len >= sizeof(CallJoinPayload)) {
                handle_call_join(conn, (const CallJoinPayload*)payload);
            }
            break;
        case CLIENT_CALL_CONN_LEAVE:
            if (payload_len >= sizeof(CallJoinPayload)) {
                handle_call_leave(conn, (const CallJoinPayload*)payload);
            }
            break;
            
        default:
            printf("ERROR: Unknown message type 0x%02x from connection %d\n", message_type, conn->fd);
            break;
    }
}

// ==================== ОБРАБОТЧИКИ UDP ПАКЕТОВ ====================

void handle_udp_packet(const uint8_t* data, size_t len, const struct sockaddr_in* src_addr) {
    if (len < UDP_HEADER_SIZE) {
        printf("UDP packet too small: %zu bytes\n", len);
        return;
    }
    
    // Определяем тип пакета по первым байтам
    if (len >= sizeof(UDPHandshakePacket) && memcmp(data, "\0\0\0\0\0\0\0\0", 8) == 0) {
        handle_udp_handshake((const UDPHandshakePacket*)data, src_addr);
    } else {
        handle_udp_stream_packet((const UDPStreamPacket*)data, src_addr);
    }
}

void handle_udp_handshake(const UDPHandshakePacket* packet, const struct sockaddr_in* src_addr) {
    uint32_t connection_id = ntohl(packet->connection_id);
    printf("UDP handshake: connection_id=%u\n", connection_id);
    
    Connection* conn = connection_find(connection_id);
    if (!conn) {
        printf("UDP handshake: connection %u not found\n", connection_id);
        return;
    }
    
    // Проверяем, что IP адрес совпадает с TCP соединением
    if (conn->tcp_addr.sin_addr.s_addr != src_addr->sin_addr.s_addr) {
        printf("UDP handshake: IP mismatch for connection %u\n", connection_id);
        return;
    }
    
    // Сохраняем UDP адрес (с портом)
    connection_set_udp_addr(conn, src_addr);
    connection_set_udp_handshake_complete(conn);
    
    // Отправляем подтверждение
    send_server_handshake_end(conn);
    
    printf("UDP handshake completed for connection %u\n", connection_id);
}

void handle_udp_stream_packet(const UDPStreamPacket* packet, const struct sockaddr_in* src_addr) {
    uint32_t call_id = ntohl(packet->call_id);
    uint32_t stream_id = ntohl(packet->stream_id);
    
    // Находим стрим
    Stream* stream = stream_find_by_id(stream_id);
    if (!stream) {
        printf("UDP stream packet: stream %u not found\n", stream_id);
        return;
    }
    
    // Для приватных стримов проверяем call_id
    if (stream->call && stream->call->call_id != call_id) {
        printf("UDP stream packet: call_id mismatch for stream %u\n", stream_id);
        return;
    }
    
    // Пересылаем пакет всем получателям
    broadcast_to_stream_recipients(stream, 0, packet, sizeof(UDPStreamPacket), NULL);
    
    (void)src_addr; // Помечаем параметр как использованный
}

// ==================== ФУНКЦИИ ОТПРАВКИ СЕРВЕРА ====================

void send_server_handshake_start(Connection* conn) {
    printf("send_server_handshake_start: ");
    print_connection_id(conn);
    printf("\n");
    
    HandshakeStartPayload payload;
    payload.connection_id = htonl(conn->fd);
    
    uint8_t message[1 + sizeof(HandshakeStartPayload)];
    message[0] = SERVER_HANDSHAKE_START;
    memcpy(message + 1, &payload, sizeof(payload));
    
    connection_send_message(conn, message, sizeof(message));
}

void send_server_handshake_end(Connection* conn) {
    printf("send_server_handshake_end: ");
    print_connection_id(conn);
    printf("\n");
    
    HandshakeStartPayload payload;  // Используем ту же структуру, что и для START
    payload.connection_id = htonl(conn->fd);
    
    uint8_t message[1 + sizeof(HandshakeStartPayload)];
    message[0] = SERVER_HANDSHAKE_END;
    memcpy(message + 1, &payload, sizeof(payload));
    
    connection_send_message(conn, message, sizeof(message));
}

void send_stream_created(Connection* conn, Stream* stream) {
    printf("send_stream_created: ");
    print_connection_id(conn);
    printf(", ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    StreamIDPayload payload;
    payload.stream_id = htonl(stream->stream_id);
    
    uint8_t message[1 + sizeof(StreamIDPayload)];
    message[0] = SERVER_STREAM_CREATED;
    memcpy(message + 1, &payload, sizeof(payload));
    
    connection_send_message(conn, message, sizeof(message));
}

void send_stream_deleted(Stream* stream) {
    printf("send_stream_deleted: ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    StreamIDPayload payload;
    payload.stream_id = htonl(stream->stream_id);
    
    broadcast_to_stream_recipients(stream, SERVER_STREAM_DELETED, &payload, sizeof(payload), NULL);
}

void send_stream_joined(Connection* conn, Stream* stream) {
    printf("send_stream_joined: ");
    print_connection_id(conn);
    printf(", ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    StreamIDPayload payload;
    payload.stream_id = htonl(stream->stream_id);
    
    uint8_t message[1 + sizeof(StreamIDPayload)];
    message[0] = SERVER_STREAM_CONN_JOINED;
    memcpy(message + 1, &payload, sizeof(payload));
    
    connection_send_message(conn, message, sizeof(message));
}

void send_stream_start(Stream* stream) {
    printf("send_stream_start: ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    StreamIDPayload payload;
    payload.stream_id = htonl(stream->stream_id);
    
    uint8_t message[1 + sizeof(StreamIDPayload)];
    message[0] = SERVER_STREAM_START;
    memcpy(message + 1, &payload, sizeof(payload));
    
    // Приводим тип для устранения предупреждения
    connection_send_message((Connection*)stream->owner, message, sizeof(message));
}

void send_stream_end(Stream* stream) {
    printf("send_stream_end: ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    StreamIDPayload payload;
    payload.stream_id = htonl(stream->stream_id);
    
    uint8_t message[1 + sizeof(StreamIDPayload)];
    message[0] = SERVER_STREAM_END;
    memcpy(message + 1, &payload, sizeof(payload));
    
    // Приводим тип для устранения предупреждения
    connection_send_message((Connection*)stream->owner, message, sizeof(message));
}

void send_call_created(Connection* conn, Call* call) {
    printf("send_call_created: ");
    print_connection_id(conn);
    printf(", ");
    print_call_id(call->call_id);
    printf("\n");
    
    IDPayload payload;
    payload.id = htonl(call->call_id);
    
    uint8_t message[1 + sizeof(IDPayload)];
    message[0] = SERVER_CALL_CREATED;
    memcpy(message + 1, &payload, sizeof(payload));
    
    connection_send_message(conn, message, sizeof(message));
}

void send_call_joined(Connection* conn, Call* call) {
    printf("send_call_joined: ");
    print_connection_id(conn);
    printf(", ");
    print_call_id(call->call_id);
    printf("\n");
    
    // Подготавливаем данные участников и стримов
    int participant_count = call_get_participant_count(call);
    int stream_count = call_get_stream_count(call);
    
    CallJoinedPayload header;
    header.call_id = htonl(call->call_id);
    header.participant_count = (uint8_t)participant_count;
    header.stream_count = (uint8_t)stream_count;
    
    // Вычисляем общий размер
    size_t total_size = 1 + sizeof(CallJoinedPayload) + 
                       participant_count * sizeof(uint32_t) + 
                       stream_count * sizeof(uint32_t);
    
    uint8_t* message = malloc(total_size);
    if (!message) return;
    
    message[0] = SERVER_CALL_CONN_JOINED;
    memcpy(message + 1, &header, sizeof(header));
    
    // Копируем участников
    uint32_t* participants_ptr = (uint32_t*)(message + 1 + sizeof(header));
    for (int i = 0; i < MAX_CALL_PARTICIPANTS; i++) {
        if (call->participants[i]) {
            *participants_ptr++ = htonl(call->participants[i]->fd);
        }
    }
    
    // Копируем стримы
    uint32_t* streams_ptr = participants_ptr;
    for (int i = 0; i < MAX_CALL_STREAMS; i++) {
        if (call->streams[i]) {
            *streams_ptr++ = htonl(call->streams[i]->stream_id);
        }
    }
    
    connection_send_message(conn, message, total_size);
    free(message);
}

void send_call_conn_new(Call* call, Connection* new_conn) {
    printf("send_call_conn_new: ");
    print_call_id(call->call_id);
    printf(", new_conn=%d\n", new_conn->fd);
    
    CallConnPayload payload;
    payload.call_id = htonl(call->call_id);
    payload.connection_id = htonl(new_conn->fd);
    
    broadcast_to_call_participants(call, SERVER_CALL_CONN_NEW, &payload, sizeof(payload), new_conn);
}

void send_call_conn_left(Call* call, Connection* left_conn) {
    printf("send_call_conn_left: ");
    print_call_id(call->call_id);
    printf(", left_conn=%d\n", left_conn->fd);
    
    CallConnPayload payload;
    payload.call_id = htonl(call->call_id);
    payload.connection_id = htonl(left_conn->fd);
    
    broadcast_to_call_participants(call, SERVER_CALL_CONN_LEFT, &payload, sizeof(payload), left_conn);
}

void send_call_stream_new(Call* call, Stream* stream) {
    printf("send_call_stream_new: ");
    print_call_id(call->call_id);
    printf(", ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    CallStreamPayload payload;
    payload.call_id = htonl(call->call_id);
    payload.stream_id = htonl(stream->stream_id);
    
    broadcast_to_call_participants(call, SERVER_CALL_STREAM_NEW, &payload, sizeof(payload), NULL);
}

void send_call_stream_deleted(Call* call, Stream* stream) {
    printf("send_call_stream_deleted: ");
    print_call_id(call->call_id);
    printf(", ");
    print_stream_id(stream->stream_id);
    printf("\n");
    
    CallStreamPayload payload;
    payload.call_id = htonl(call->call_id);
    payload.stream_id = htonl(stream->stream_id);
    
    broadcast_to_call_participants(call, SERVER_CALL_STREAM_DELETED, &payload, sizeof(payload), NULL);
}

// ==================== СЛУЖЕБНЫЕ ФУНКЦИИ ====================

void handle_connection_closed(Connection* conn) {
    printf("handle_connection_closed: ");
    print_connection_id(conn);
    printf("\n");
    
    connection_delete(conn);
}

void broadcast_to_stream_recipients(Stream* stream, uint8_t message_type, const void* payload, size_t payload_len, Connection* exclude) {
    if (!stream) return;
    
    // Создаем полное сообщение
    uint8_t* message = malloc(1 + payload_len);
    if (!message) return;
    
    message[0] = message_type;
    if (payload_len > 0) {
        memcpy(message + 1, payload, payload_len);
    }
    
    // Отправляем всем получателям кроме исключенного
    for (int i = 0; i < STREAM_MAX_RECIPIENTS; i++) {
        Connection* recipient = stream->recipients[i];
        if (recipient && recipient != exclude) {
            connection_send_message(recipient, message, 1 + payload_len);
        }
    }
    
    free(message);
}

void broadcast_to_call_participants(Call* call, uint8_t message_type, const void* payload, size_t payload_len, Connection* exclude) {
    if (!call) return;
    
    // Создаем полное сообщение
    uint8_t* message = malloc(1 + payload_len);
    if (!message) return;
    
    message[0] = message_type;
    if (payload_len > 0) {
        memcpy(message + 1, payload, payload_len);
    }
    
    // Отправляем всем участникам кроме исключенного
    for (int i = 0; i < MAX_CALL_PARTICIPANTS; i++) {
        Connection* participant = call->participants[i];
        if (participant && participant != exclude) {
            connection_send_message(participant, message, 1 + payload_len);
        }
    }
    
    free(message);
}