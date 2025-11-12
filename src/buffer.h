#pragma once
#include <stdint.h>
#include <stddef.h>

#define BUFFER_SIZE 8192

typedef struct {
    uint8_t data[BUFFER_SIZE];
    uint32_t position;      // количество реально записанных байт
    uint32_t expected_size; // используется для "полноты", но может быть 0, если не задано
} Buffer;

/* Коды возврата для buffer_read / buffer_write */
typedef enum {
    BUFFER_IS_COMPLETE   =  1,  // position == expected_size
    BUFFER_IS_INCOMPLETE =  0,  // position < expected_size
    BUFFER_MEMORY_OVERFLOW = -1, // попытка записи за пределы BUFFER_SIZE
    BUFFER_OVERFLOW        = -2  // position превысил expected_size
} BufferResult;

/* Инициализация */
void buffer_init(Buffer* buf);
void buffer_clear(Buffer* buf);

/* Установка ожидаемого размера */
int buffer_reserve(Buffer* buf, uint32_t n); // возвращает 0 при успехе, -1 при n > BUFFER_SIZE

/* Низкоуровневые операции (аналог memcpy)
 * Возвращают один из кодов BufferResult
 */
BufferResult buffer_write(Buffer* restrict dest, const void* restrict src, uint32_t n);
BufferResult buffer_read(Buffer* restrict src, void* restrict dest, uint32_t n);

BufferResult buffer_state(const Buffer* buf);
