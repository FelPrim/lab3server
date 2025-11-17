#pragma once
#include <stdint.h>
#include "buffer.h"
#include "protocol.h"

/*
 * buffer_protocol — слой, который знает структуру сообщений.
 * Его задача:
 *   - определить ожидаемый размер (expected_size) по первому байту — типу сообщения
 *   - проверять, полное ли сообщение получено
 *   - сбрасывать буфер после обработки полного сообщения
 */

/* Инициализация expected_size по типу из buf->data[0].
 * Возвращает:
 *   0  — успех, expected_size установлен
 *  -1  — недостаточно данных (в буфере нет даже 1 байта)
 *  -2  — неизвестный тип сообщения
 *  -3  — аргументы некорректны
 */
int buffer_protocol_set_expected(Buffer* buf);

/* Возвращает ожидаемый размер для данного типа сообщения.
 * Возвращает 0 при успехе, -1 при неизвестном типе.
 */
int buffer_protocol_expected_size(uint8_t type, uint32_t* out_size);

/* Проверяет состояние сообщения (использует buffer_state). */
BufferResult buffer_protocol_state(const Buffer* buf);

/* Очищает сообщение, если оно полностью прочитано. */
void buffer_protocol_consume(Buffer* buf);
// Добавьте в конец buffer_logic.h
size_t buffer_get_data_size(Buffer* buf);