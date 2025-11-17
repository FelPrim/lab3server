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
    uint32_t stream_id;                          
    Connection* owner;                           
    Connection* recipients[STREAM_MAX_RECIPIENTS]; 
    uint32_t recipient_count;                    
    UT_hash_handle hh;                           
} Stream;

/* Глобальная хеш-таблица стримов */
extern Stream* streams;

/* Базовые операции с памятью */
Stream* stream_alloc(uint32_t stream_id, Connection* owner);
void stream_free(Stream* stream);

/* Управление получателями (только массив, без логики connection) */
int stream_add_recipient_to_array(Stream* stream, Connection* recipient);
int stream_remove_recipient_from_array(Stream* stream, Connection* recipient);
int stream_is_recipient_in_array(const Stream* stream, const Connection* recipient);
void stream_clear_recipients_array(Stream* stream);

/* Реестр стримов */
int stream_add_to_registry(Stream* stream);
int stream_remove_from_registry(Stream* stream);
Stream* stream_find_by_id(uint32_t stream_id);

/* Поиск */
int stream_find_by_owner_in_registry(const Connection* owner, Stream** result, int max_results);
int stream_find_by_recipient_in_registry(const Connection* recipient, Stream** result, int max_results);

/* Генерация ID */
uint32_t stream_generate_id(void);