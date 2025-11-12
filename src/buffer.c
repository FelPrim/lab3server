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
    assert(dest);
    assert(src);

    /* Проверяем на переполнение памяти */
    if (dest->position + n > BUFFER_SIZE)
        return BUFFER_MEMORY_OVERFLOW;

    memcpy(dest->data + dest->position, src, n);
    dest->position += n;

    /* Проверка на переполнение логического размера */
    if (dest->expected_size > 0 && dest->position > dest->expected_size)
        return BUFFER_OVERFLOW;

    /* Проверка на завершённость */
    if (dest->expected_size > 0 && dest->position == dest->expected_size)
        return BUFFER_IS_COMPLETE;

    return BUFFER_IS_INCOMPLETE;
}

BufferResult buffer_read(Buffer* restrict src, void* restrict dest, uint32_t n) {
    assert(src);
    assert(dest);

    /* Если буфер пуст */
    if (src->position == 0)
        return BUFFER_IS_INCOMPLETE;

    /* Сколько реально можно прочитать */
    uint32_t to_read = n;
    if (to_read > src->position)
        to_read = src->position;

    /* Если задан expected_size, не читаем больше него */
    if (src->expected_size > 0 && to_read > src->expected_size)
        to_read = src->expected_size;

    memcpy(dest, src->data, to_read);

    /* Сдвигаем оставшиеся данные */
    memmove(src->data, src->data + to_read, src->position - to_read);
    src->position -= to_read;

    /* Проверка логического состояния */
    if (src->expected_size > 0 && src->position > src->expected_size)
        return BUFFER_OVERFLOW;

    if (src->expected_size > 0 && src->position == src->expected_size)
        return BUFFER_IS_COMPLETE;

    return BUFFER_IS_INCOMPLETE;
}

BufferResult buffer_state(const Buffer* buf) {
    assert(buf);

    if (buf->expected_size == 0)
        return BUFFER_IS_INCOMPLETE; // не задано, значит неполный

    if (buf->position < buf->expected_size)
        return BUFFER_IS_INCOMPLETE;

    if (buf->position == buf->expected_size)
        return BUFFER_IS_COMPLETE;

    return BUFFER_OVERFLOW;
}
