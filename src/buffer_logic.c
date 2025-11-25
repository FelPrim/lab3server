#include "buffer_logic.h"
#include "protocol.h"  // Добавляем для новых типов сообщений
#include <string.h>

// Mock функция для определения размера сообщений по типу
int buffer_protocol_expected_size(uint8_t type, uint32_t* out_size) {
    if (!out_size)
        return -1;

    switch (type) {
        /* Базовые сообщения */
        case CLIENT_ERROR:
        case SERVER_ERROR:
        case CLIENT_SUCCESS:
        case SERVER_SUCCESS:
            // Переменная длина - минимальный размер 2 байта (тип + длина)
            *out_size = 2;
            return 0;

        case SERVER_HANDSHAKE_START:
            *out_size = 1 + sizeof(HandshakeStartPayload);
            return 0;

        case SERVER_HANDSHAKE_END:
            *out_size = 1 + sizeof(HandshakeEndPayload);
            return 0;

        /* Сообщения стримов */
        case CLIENT_STREAM_CREATE:
            *out_size = 1 + sizeof(StreamCreatePayload);
            return 0;

        case CLIENT_STREAM_DELETE:
        case CLIENT_STREAM_CONN_JOIN:
        case CLIENT_STREAM_CONN_LEAVE:
        case SERVER_STREAM_CREATED:
        case SERVER_STREAM_DELETED:
        case SERVER_STREAM_CONN_JOINED:
        case SERVER_STREAM_START:
        case SERVER_STREAM_END:
            *out_size = 1 + sizeof(StreamIDPayload);
            return 0;

        /* Сообщения звонков */
        case CLIENT_CALL_CREATE:
            *out_size = 1;  // Только тип
            return 0;

        case CLIENT_CALL_CONN_JOIN:
        case CLIENT_CALL_CONN_LEAVE:
            *out_size = 1 + sizeof(CallJoinPayload);
            return 0;

        case SERVER_CALL_CREATED:
        case SERVER_CALL_CONN_NEW:
        case SERVER_CALL_CONN_LEFT:
        case SERVER_CALL_STREAM_NEW:
        case SERVER_CALL_STREAM_DELETED:
            *out_size = 1 + sizeof(IDPayload);
            return 0;

        case SERVER_CALL_CONN_JOINED:
            // Переменная длина - зависит от количества участников и стримов
            // Минимальный размер: тип + call_id + participant_count + stream_count
            *out_size = 1 + sizeof(uint32_t) + 1 + 1;
            return 0;

        default:
            return -1;  // Неизвестный тип сообщения
    }
}

// Добавить в начало buffer_logic.c
static int (*current_resolver)(uint8_t, uint32_t*) = buffer_protocol_expected_size;

void buffer_logic_set_expected_size_resolver(int (*resolver)(uint8_t, uint32_t*)) {
    current_resolver = resolver;
}

// Изменить функцию buffer_protocol_set_expected в buffer_logic.c:
int buffer_protocol_set_expected(Buffer* buf) {
    if (!buf)
        return -3;
    if (buf->expected_size > 0)
        return 0;
    if (buf->position < 1)
        return -1; // пока нет типа

    uint8_t type = buf->data[0];
    uint32_t size = 0;
    
    // Использовать current_resolver вместо прямой ссылки
    if (current_resolver(type, &size) != 0)
        return -2;

    if (size > BUFFER_SIZE)
        return -2;

    buf->expected_size = size;
    return 0;
}

BufferResult buffer_protocol_state(const Buffer* buf) {
    if (!buf)
        return BUFFER_IS_INCOMPLETE;
    return buffer_state(buf);
}

void buffer_protocol_consume(Buffer* buf) {
    if (!buf)
        return;
    /* Если сообщение полное, просто очищаем буфер */
    if (buffer_state(buf) == BUFFER_IS_COMPLETE || buffer_state(buf) == BUFFER_OVERFLOW) {
        buffer_clear(buf);
    }
}

// Добавьте в конец buffer_logic.c
size_t buffer_get_data_size(Buffer* buf) {
    if (!buf) return 0;
    return buf->position;
}