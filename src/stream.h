#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "uthash.h"

// Limits
#ifndef MAX_INPUT
#define MAX_INPUT 4
#endif

#ifndef MAX_OUTPUT
#define MAX_OUTPUT 4
#endif

#ifndef STREAM_MAX_RECIPIENTS
#define STREAM_MAX_RECIPIENTS 4
#endif

// Forward declaration
typedef struct Connection Connection;

typedef struct Stream {
    uint32_t stream_id;                          // уникальный идентификатор трансляции
    Connection* owner;                           // владелец трансляции (может быть NULL)
    Connection* recipients[STREAM_MAX_RECIPIENTS]; // плотный массив получателей (NULL-terminated)
    uint32_t recipient_count;                    // число получателей
    UT_hash_handle hh;                           // для хеш-таблицы (ключ = stream_id)
} Stream;

/* Глобальная хеш-таблица стримов (ключ - stream_id) */
extern Stream* streams;

/* Создание/удаление стрима.
 * stream_create возвращает NULL в случае ошибки (например, выделение памяти или owner == NULL).
 * stream_destroy освобождает память и корректно очищает все связанные ссылки.
 * Все функции в заголовочных файлах проверяют входные параметры и не вызывают аварийного завершения. */
Stream* stream_create(uint32_t stream_id, Connection* owner);
void stream_destroy(Stream* stream);

/* Работа с получателями */
int stream_add_recipient(Stream* stream, Connection* recipient);
int stream_remove_recipient(Stream* stream, Connection* recipient);
int stream_is_recipient(const Stream* stream, const Connection* recipient);
void stream_clear_recipients(Stream* stream);

/* Регистрация в глобальном реестре стримов */
int stream_add_to_registry(Stream* stream);
int stream_remove_from_registry(Stream* stream);
Stream* stream_find_by_id(uint32_t stream_id);

/* Поиск */
int stream_find_by_owner(const Connection* owner, Stream** result, int max_results);
int stream_find_by_recipient(const Connection* recipient, Stream** result, int max_results);

/* Генерация ID (безопасно для множественных вызовов) */
uint32_t stream_generate_id(void);
