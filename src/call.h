#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "uthash.h"
#include "dense_array.h"

typedef struct Connection Connection;
typedef struct Stream Stream;

#define MAX_CALL_PARTICIPANTS 4
#define MAX_CALL_STREAMS 4

typedef struct Call {
    uint32_t call_id;
    Connection* participants[MAX_CALL_PARTICIPANTS];
    Stream* streams[MAX_CALL_STREAMS];
    UT_hash_handle hh;
} Call;

extern Call* calls;

Call* call_alloc(uint32_t call_id);
void call_free(Call* call);

int call_add_participant_to_array(Call* call, Connection* participant);
int call_remove_participant_from_array(Call* call, Connection* participant);
int call_is_participant_in_array(const Call* call, const Connection* participant);
void call_clear_participants_array(Call* call);

/* Управление стримами (только массив, без логики stream) */
int call_add_stream_to_array(Call* call, Stream* stream);
int call_remove_stream_from_array(Call* call, Stream* stream);
int call_is_stream_in_array(const Call* call, const Stream* stream);
void call_clear_streams_array(Call* call);

/* Реестр звонков */
int call_add_to_registry(Call* call);
int call_remove_from_registry(Call* call);
Call* call_find_by_id(uint32_t call_id);

/* Поиск */
int call_find_by_participant_in_registry(const Connection* participant, Call** result, int max_results);

/* Генерация ID */
uint32_t call_generate_id(void);
