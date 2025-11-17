#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>  // Добавлено для struct sockaddr
#include <netinet/in.h>   // Добавлено для struct sockaddr_in

// Forward declarations
typedef struct Connection Connection;
typedef struct Stream Stream;

// ==================== КОМАНДЫ ОТ КЛИЕНТА К СЕРВЕРУ ====================
#define CLIENT_UDP_ADDR          0x01  // Посылка UDP-адреса
#define CLIENT_DISCONNECT        0x02  // Разрыв соединения (сообщение)
#define CLIENT_STREAM_CREATE     0x03  // Запрос на создание трансляции
#define CLIENT_STREAM_DELETE     0x04  // Запрос на завершение трансляции  
#define CLIENT_STREAM_JOIN       0x05  // Запрос на подсоединение к трансляции
#define CLIENT_STREAM_LEAVE      0x06  // Запрос на выход из трансляции

// ==================== КОМАНДЫ ОТ СЕРВЕРА К КЛИЕНТУ ====================
#define SERVER_STREAM_CREATED    0x81  // Ответ: создана трансляция
#define SERVER_STREAM_DELETED    0x82  // Уведомление: трансляция закрыта
#define SERVER_STREAM_JOINED     0x83  // Ответ на запрос подсоединения
#define SERVER_STREAM_START      0x84  // Уведомление: начать передачу (есть зрители)
#define SERVER_STREAM_END        0x85  // Уведомление: прекратить передачу (нет зрителей)
#define SERVER_NEW_RECIPIENT     0x86  // Уведомление: новый получатель
#define SERVER_RECIPIENT_LEFT    0x87  // Уведомление: получатель вышел

// ==================== ФОРМАТЫ СООБЩЕНИЙ ====================

#pragma pack(push, 1)
// Новая структура для 16-байтного sockaddr_in
typedef struct {
    uint16_t family;  // 2 байта - sin_family
    uint16_t port;    // 2 байта - sin_port (сетевой порядок)
    uint32_t ip;      // 4 байта - sin_addr (сетевой порядок)
    uint8_t zero[8];  // 8 байт - sin_zero
} UDPAddrFullPayload;

// Старая структура оставляем для обратной совместимости
typedef struct {
    uint16_t port;    // сетевой порядок
    uint32_t ip;      // IPv4 в сетевом порядке
} UDPAddrPayload;
#pragma pack(pop)

// CLIENT_STREAM_CREATE: [1 байт] (без данных)

// CLIENT_STREAM_DELETE/CLIENT_STREAM_JOIN/CLIENT_STREAM_LEAVE: [1 байт] + [4 байта: ID трансляции]
typedef struct {
    uint32_t stream_id;  // ID трансляции
} StreamIDPayload;

// SERVER_STREAM_CREATED/SERVER_STREAM_DELETED/SERVER_STREAM_JOINED: [1 байт] + [4 байта: ID трансляции]
// (аналогично StreamIDPayload)

// SERVER_STREAM_START/SERVER_STREAM_END: [1 байт] + [4 байта: ID трансляции]
// (аналогично StreamIDPayload)

// SERVER_NEW_RECIPIENT/SERVER_RECIPIENT_LEFT: [1 байт] + [4 байта: ID трансляции] + [6 байт: адрес получателя]
typedef struct {
    uint32_t stream_id;
    UDPAddrPayload recipient_addr;
} RecipientNotificationPayload;
#pragma pack(pop)

// ==================== ОБРАБОТЧИКИ TCP СООБЩЕНИЙ ОТ КЛИЕНТА ====================

// Диспетчер входящих сообщений
void handle_client_message(Connection* conn, uint8_t message_type, const uint8_t* payload, size_t payload_len);

// Конкретные обработчики
void handle_udp_addr(Connection* conn, const UDPAddrFullPayload* payload);
void handle_disconnect(Connection* conn);
void handle_stream_create(Connection* conn);
void handle_stream_delete(Connection* conn, const StreamIDPayload* payload);
void handle_stream_join(Connection* conn, const StreamIDPayload* payload);
void handle_stream_leave(Connection* conn, const StreamIDPayload* payload);

// ==================== ОБРАБОТЧИКИ UDP ПАКЕТОВ ====================

// Обработчик входящих UDP пакетов
void handle_udp_packet(const uint8_t* data, size_t len, const struct sockaddr_in* src_addr);

// ==================== ФУНКЦИИ ОТПРАВКИ ОТВЕТОВ КЛИЕНТАМ ====================

// Отправка уведомлений о трансляциях
void send_stream_created(Connection* conn, const Stream* stream);
void send_stream_deleted_to_recipients(const Stream* stream);
void send_join_result(Connection* conn, const Stream* stream, int result);
void send_stream_start_to_owner(const Stream* stream);
void send_stream_end_to_owner(const Stream* stream);

// Отправка уведомлений о участниках трансляций
void send_new_recipient_to_stream(const Stream* stream, const Connection* new_recipient);
void send_recipient_left_to_stream(const Stream* stream, const Connection* left_recipient);

// ==================== СЛУЖЕБНЫЕ ФУНКЦИИ ====================

// Обработка отключения клиента
void handle_connection_closed(Connection* conn);

// Рассылка сообщения всем получателям трансляции
void broadcast_to_stream(const Stream* stream, uint8_t message_type, const void* payload, size_t payload_len, const Connection* exclude);