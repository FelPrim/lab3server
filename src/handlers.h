#pragma once

#include "connection.h"
#include "stream.h"
#include "protocol.h"

// ==================== ОБРАБОТЧИКИ TCP СООБЩЕНИЙ ОТ КЛИЕНТА ====================

// Диспетчер входящих сообщений
void handle_client_message(Connection* conn, uint8_t message_type, const uint8_t* payload, size_t payload_len);

// Конкретные обработчики
void handle_udp_addr(Connection* conn, const UDPAddrPayload* payload);
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

// Вспомогательные функции для работы с трансляциями
int create_stream_for_connection(Connection* conn, uint32_t stream_id);
int add_recipient_to_stream(Stream* stream, Connection* recipient);
int remove_recipient_from_stream(Stream* stream, Connection* recipient);
void cleanup_streams_on_disconnect(Connection* conn);