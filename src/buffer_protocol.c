#include "buffer_protocol.h"
#include <string.h>

int buffer_protocol_expected_size(uint8_t type, uint32_t* out_size) {
    if (!out_size)
        return -1;

    switch (type) {
        /* Клиентские сообщения */
        case CLIENT_UDP_ADDR:
            *out_size = 1 + sizeof(UDPAddrPayload); // тип + payload
            return 0;
        case CLIENT_DISCONNECT:
        case CLIENT_STREAM_CREATE:
            *out_size = 1;
            return 0;
        case CLIENT_STREAM_DELETE:
        case CLIENT_STREAM_JOIN:
        case CLIENT_STREAM_LEAVE:
            *out_size = 1 + sizeof(StreamIDPayload);
            return 0;

        /* Серверные сообщения */
        case SERVER_STREAM_CREATED:
        case SERVER_STREAM_DELETED:
        case SERVER_STREAM_JOINED:
        case SERVER_STREAM_START:
        case SERVER_STREAM_END:
            *out_size = 1 + sizeof(StreamIDPayload);
            return 0;

        default:
            return -1;
    }
}

int buffer_protocol_set_expected(Buffer* buf) {
    if (!buf)
        return -3;
    if (buf->expected_size > 0)
        return 0;
    if (buf->position < 1)
        return -1; // пока нет типа

    uint8_t type = buf->data[0];
    uint32_t size = 0;
    if (buffer_protocol_expected_size(type, &size) != 0)
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
