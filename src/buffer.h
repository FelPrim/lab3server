// buffer.h - добавить в конец файла
#pragma once
#include <stdint.h>
#include <stddef.h>

#define BUFFER_SIZE 8192

typedef struct {
    uint8_t data[BUFFER_SIZE];
    uint32_t position;      
    uint32_t expected_size; 
} Buffer;

typedef enum {
    BUFFER_IS_COMPLETE   =  1,
    BUFFER_IS_INCOMPLETE =  0,
    BUFFER_MEMORY_OVERFLOW = -1,
    BUFFER_OVERFLOW        = -2
} BufferResult;

void buffer_init(Buffer* buf);
void buffer_clear(Buffer* buf);
int buffer_reserve(Buffer* buf, uint32_t n);
BufferResult buffer_write(Buffer* restrict dest, const void* restrict src, uint32_t n);
BufferResult buffer_read(Buffer* restrict src, void* restrict dest, uint32_t n);
BufferResult buffer_state(const Buffer* buf);