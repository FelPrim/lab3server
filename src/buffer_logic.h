#pragma once

#include "buffer.h"

// Функция для определения ожидаемого размера сообщения по его типу
int buffer_protocol_expected_size(uint8_t type, uint32_t* out_size);

// Установка пользовательского резолвера размеров
void buffer_logic_set_expected_size_resolver(int (*resolver)(uint8_t, uint32_t*));

// Установка expected_size на основе данных в буфере
int buffer_protocol_set_expected(Buffer* buf);

// Проверка состояния буфера в контексте протокола
BufferResult buffer_protocol_state(const Buffer* buf);

// Потребление (очистка) обработанного сообщения
void buffer_protocol_consume(Buffer* buf);

// Получение текущего размера данных в буфере
size_t buffer_get_data_size(Buffer* buf);