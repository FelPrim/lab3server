#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "uthash.h"
#include "dense_array.h"
#include "connection.h"
#include "stream.h"

#define MAX_CALL_PARTICIPANTS 4
#define MAX_CALL_STREAMS 4

typedef struct Call {
    uint32_t call_id;
    Connection* participants[MAX_CALL_PARTICIPANTS];
    Stream* streams[MAX_CALL_STREAMS];
    UT_hash_handle hh;
} Call;

extern Call* calls;

/* Основные операции жизненного цикла */
Call* call_new(uint32_t call_id);
void call_delete(Call* call);

/* Управление участниками */
int call_add_participant(Call* call, Connection* participant);
int call_remove_participant(Call* call, Connection* participant);
bool call_has_participant(const Call* call, const Connection* participant);
int call_get_participant_count(const Call* call);

/* Управление стримами */
int call_add_stream(Call* call, Stream* stream);
int call_remove_stream(Call* call, Stream* stream);
bool call_has_stream(const Call* call, const Stream* stream);
int call_get_stream_count(const Call* call);

/* Поиск */
Call* call_find_by_id(uint32_t call_id);
int call_find_by_participant(const Connection* participant, Call** result, int max_results);

/* Утилиты */
Connection* call_find_participant_by_id(const Call* call, uint32_t connection_id);
Stream* call_find_stream_by_id(const Call* call, uint32_t stream_id);

bool call_can_add_participant(const Call* call);
bool call_can_add_stream(const Call* call);
int call_remove_participant_safe(Call* call, Connection* participant);