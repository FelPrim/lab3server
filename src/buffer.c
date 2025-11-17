#include "buffer.h"
#include <string.h>
#include <assert.h>

void buffer_init(Buffer* buf) {
    assert(buf);
    buf->position = 0;
    buf->expected_size = 0;
    memset(buf->data, 0, BUFFER_SIZE);
}

void buffer_clear(Buffer* buf) {
    buffer_init(buf);
}

int buffer_reserve(Buffer* buf, uint32_t n) {
    assert(buf);
    if (n > BUFFER_SIZE) return -1;
    buf->expected_size = n;
    buf->position = 0;
    return 0;
}

BufferResult buffer_write(Buffer* restrict dest, const void* restrict src, uint32_t n) {
    if (!dest) return BUFFER_MEMORY_OVERFLOW;

    if (n == 0) {
        if (dest->expected_size > 0 && dest->position == dest->expected_size)
            return BUFFER_IS_COMPLETE;
        if (dest->expected_size > 0 && dest->position < dest->expected_size)
            return BUFFER_IS_INCOMPLETE;
        return BUFFER_IS_INCOMPLETE;
    }

    if ((uint64_t)dest->position + (uint64_t)n > (uint64_t)BUFFER_SIZE) {
        return BUFFER_MEMORY_OVERFLOW;
    }

    if (dest->expected_size > 0) {
        uint32_t allowed = (dest->expected_size > dest->position) ? (dest->expected_size - dest->position) : 0;
        if (n > allowed) {
            return BUFFER_OVERFLOW;
        }

        memcpy(dest->data + dest->position, src, n);
        dest->position += n;
        if (dest->position == dest->expected_size) return BUFFER_IS_COMPLETE;
        return BUFFER_IS_INCOMPLETE;
    }

    memcpy(dest->data + dest->position, src, n);
    dest->position += n;
    return BUFFER_IS_INCOMPLETE;
}

BufferResult buffer_read(Buffer* restrict src, void* restrict dest, uint32_t n) {
    if (!src || (!dest && n > 0)) 
        return BUFFER_MEMORY_OVERFLOW;

    if (n == 0) 
        return buffer_state(src);

    if (src->position == 0) 
        return BUFFER_IS_INCOMPLETE;

    // Проверяем логическое переполнение (если expected_size задан)
    if (src->expected_size > 0 && n > src->expected_size) {
        return BUFFER_OVERFLOW;
    }

    // Проверяем, не пытаемся ли прочитать больше чем есть
    if (n > src->position) {
        return BUFFER_OVERFLOW;
    }

    // Копируем данные
    memcpy(dest, src->data, n);

    // Сдвигаем оставшиеся данные
    uint32_t remaining = src->position - n;
    if (remaining > 0) {
        memmove(src->data, src->data + n, remaining);
    }
    
    src->position = remaining;

    // Обновляем expected_size - это важно!
    if (src->expected_size > 0) {
        if (n >= src->expected_size) {
            src->expected_size = 0;
        } else {
            src->expected_size -= n;
        }
    }

    return buffer_state(src);
}

BufferResult buffer_state(const Buffer* buf) {
    assert(buf);

    if (buf->expected_size == 0)
        return BUFFER_IS_INCOMPLETE;

    if (buf->position < buf->expected_size)
        return BUFFER_IS_INCOMPLETE;

    if (buf->position == buf->expected_size)
        return BUFFER_IS_COMPLETE;

    return BUFFER_OVERFLOW;
}