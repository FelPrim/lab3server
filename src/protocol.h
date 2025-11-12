#pragma once

#include <stdint.h>
#include <stddef.h>

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

// CLIENT_UDP_ADDR: [1 байт] + [6 байт: порт (2) + IPv4 (4)]
#pragma pack(push, 1)
typedef struct {
    uint16_t port;    // сетевой порядок
    uint32_t ip;      // IPv4 в сетевом порядке
} UDPAddrPayload;

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