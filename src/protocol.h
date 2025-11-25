#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "connection.h"
#include "stream.h"
#include "call.h"
#include "network.h"
#include "buffer.h"  // Для BUFFER_SIZE

// ==================== КОНСТАНТЫ ПРОТОКОЛА ====================
#define UDP_PACKET_SIZE          1200
#define UDP_HANDSHAKE_ZERO_BYTES 8
#define UDP_HEADER_SIZE          (sizeof(uint32_t) * 3)  // call_id + stream_id + packet_number
#define UDP_DATA_SIZE            (UDP_PACKET_SIZE - UDP_HEADER_SIZE)

// ==================== БАЗОВЫЕ ТИПЫ СООБЩЕНИЙ ====================
#define CLIENT_ERROR              0x01
#define SERVER_ERROR              0x02
#define CLIENT_SUCCESS            0x03
#define SERVER_SUCCESS            0x04
#define SERVER_HANDSHAKE_START    0x05
#define SERVER_HANDSHAKE_END      0x06

// ==================== СООБЩЕНИЯ ДЛЯ СТРИМОВ ====================
#define CLIENT_STREAM_CREATE      0x10
#define CLIENT_STREAM_DELETE      0x11
#define CLIENT_STREAM_CONN_JOIN   0x12
#define CLIENT_STREAM_CONN_LEAVE  0x13

#define SERVER_STREAM_CREATED     0x90
#define SERVER_STREAM_DELETED     0x91
#define SERVER_STREAM_CONN_JOINED 0x92
#define SERVER_STREAM_START       0x93
#define SERVER_STREAM_END         0x94

// ==================== СООБЩЕНИЯ ДЛЯ ЗВОНКОВ ====================
#define CLIENT_CALL_CREATE        0x20
#define CLIENT_CALL_CONN_JOIN     0x21
#define CLIENT_CALL_CONN_LEAVE    0x22

#define SERVER_CALL_CREATED       0xA0
#define SERVER_CALL_CONN_JOINED   0xA1
#define SERVER_CALL_CONN_NEW      0xA2
#define SERVER_CALL_CONN_LEFT     0xA3
#define SERVER_CALL_STREAM_NEW    0xA4
#define SERVER_CALL_STREAM_DELETED 0xA5

#pragma pack(push, 1)

// Базовые структуры для ошибок/успехов
typedef struct {
    uint8_t original_message_type;
    uint8_t message_length;
    // char message[message_length] - переменная длина
} ErrorSuccessPayload;

// SERVER_HANDSHAKE_START
typedef struct {
    uint32_t connection_id;  // fd соединения
} HandshakeStartPayload;

// SERVER_HANDSHAKE_END  
typedef struct {
    uint16_t port;
} HandshakeEndPayload;

// Базовые структуры с ID
typedef struct {
    uint32_t id;
} IDPayload;

// Структуры для стримов
typedef struct {
    uint32_t call_id;  // 0 для публичного стрима
} StreamCreatePayload;

typedef struct {
    uint32_t stream_id;
} StreamIDPayload;

// Структуры для звонков
typedef struct {
    uint32_t call_id;
} CallJoinPayload;

// SERVER_CALL_CONN_JOINED
typedef struct {
    uint32_t call_id;
    uint8_t participant_count;
    uint8_t stream_count;
    // uint32_t participants[participant_count]
    // uint32_t streams[stream_count]
} CallJoinedPayload;

// SERVER_CALL_CONN_NEW / SERVER_CALL_CONN_LEFT
typedef struct {
    uint32_t call_id;
    uint32_t connection_id;  // fd соединения
} CallConnPayload;

// SERVER_CALL_STREAM_NEW / SERVER_CALL_STREAM_DELETED
typedef struct {
    uint32_t call_id;
    uint32_t stream_id;
} CallStreamPayload;

// UDP пакеты
typedef struct {
    uint64_t zero;           // UDP_HANDSHAKE_ZERO_BYTES нулевых байт
    uint32_t connection_id;  // fd соединения
} UDPHandshakePacket;

typedef struct {
    uint32_t call_id;
    uint32_t stream_id;
    uint32_t packet_number;
    uint8_t data[UDP_DATA_SIZE];
} UDPStreamPacket;

#pragma pack(pop)

// ==================== ОБРАБОТЧИКИ TCP СООБЩЕНИЙ ====================

// Главный диспетчер сообщений
void handle_client_message(Connection* conn, uint8_t message_type, const uint8_t* payload, size_t payload_len);

// Обработчики базовых сообщений
void handle_client_error(const ErrorSuccessPayload* payload);
void handle_client_success(const ErrorSuccessPayload* payload);

// Обработчики стримов
void handle_stream_create(Connection* conn, const StreamCreatePayload* payload);
void handle_stream_delete(Connection* conn, const StreamIDPayload* payload);
void handle_stream_join(Connection* conn, const StreamIDPayload* payload);
void handle_stream_leave(Connection* conn, const StreamIDPayload* payload);

// Обработчики звонков
void handle_call_create(Connection* conn);
void handle_call_join(Connection* conn, const CallJoinPayload* payload);
void handle_call_leave(Connection* conn, const CallJoinPayload* payload);

// ==================== ОБРАБОТЧИКИ UDP ПАКЕТОВ ====================

void handle_udp_packet(const uint8_t* data, size_t len, const struct sockaddr_in* src_addr);
void handle_udp_handshake(const UDPHandshakePacket* packet, const struct sockaddr_in* src_addr);
void handle_udp_stream_packet(const UDPStreamPacket* packet, const struct sockaddr_in* src_addr);

// ==================== ФУНКЦИИ ОТПРАВКИ СЕРВЕРА ====================

// Базовые сообщения
void send_server_handshake_start(Connection* conn);
void send_server_handshake_end(Connection* conn);
void send_server_error(Connection* conn, uint8_t original_message, const char* error_msg);
void send_server_success(Connection* conn, uint8_t original_message, const char* success_msg);

// Сообщения стримов
void send_stream_created(Connection* conn, Stream* stream);
void send_stream_deleted(Stream* stream);
void send_stream_joined(Connection* conn, Stream* stream);
void send_stream_start(Stream* stream);
void send_stream_end(Stream* stream);

// Сообщения звонков
void send_call_created(Connection* conn, Call* call);
void send_call_joined(Connection* conn, Call* call);
void send_call_conn_new(Call* call, Connection* new_conn);
void send_call_conn_left(Call* call, Connection* left_conn);
void send_call_stream_new(Call* call, Stream* stream);
void send_call_stream_deleted(Call* call, Stream* stream);

// ==================== СЛУЖЕБНЫЕ ФУНКЦИИ ПРОТОКОЛА ====================

void handle_connection_closed(Connection* conn);
void broadcast_to_stream_recipients(Stream* stream, uint8_t message_type, const void* payload, size_t payload_len, Connection* exclude);
void broadcast_to_call_participants(Call* call, uint8_t message_type, const void* payload, size_t payload_len, Connection* exclude);