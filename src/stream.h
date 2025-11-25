#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "uthash.h"
#include "dense_array.h"
#include "connection.h"
#include "call.h"

#ifndef STREAM_MAX_RECIPIENTS
#define STREAM_MAX_RECIPIENTS 4
#endif

typedef struct Stream {
    uint32_t stream_id;                          
    Call* call;
    Connection* owner;
    Connection* recipients[STREAM_MAX_RECIPIENTS];               
    UT_hash_handle hh;                           
} Stream;

extern Stream* streams;

/* Основные операции жизненного цикла */
Stream* stream_new(uint32_t stream_id, Connection* owner, Call* call);
void stream_delete(Stream* stream);

/* Управление получателями */
int stream_add_recipient(Stream* stream, Connection* recipient);
int stream_remove_recipient(Stream* stream, Connection* recipient);
bool stream_has_recipient(const Stream* stream, const Connection* recipient);
int stream_get_recipient_count(const Stream* stream);

/* Поиск */
Stream* stream_find_by_id(uint32_t stream_id);
int stream_find_by_owner(const Connection* owner, Stream** result, int max_results);
int stream_find_by_recipient(const Connection* recipient, Stream** result, int max_results);

/* Вспомогательные функции */
Call* stream_get_call(const Stream* stream);
bool stream_is_private(const Stream* stream);

bool stream_can_add_recipient(const Stream* stream);