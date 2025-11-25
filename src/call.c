#include "call.h"
#include "id_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

Call* calls = NULL;

/* Внутренние функции */
static Call* call_alloc(uint32_t call_id) {
    Call* call = malloc(sizeof(Call));
    if (!call) return NULL;
    
    call->call_id = call_id;
    DENSE_ARRAY_INIT(call->participants, MAX_CALL_PARTICIPANTS);
    DENSE_ARRAY_INIT(call->streams, MAX_CALL_STREAMS);
    
    return call;
}

static void call_free(Call* call) {
    if (!call) return;
    free(call);
}

static uint32_t call_generate_id(void) {
    return generate_id();
}

static int call_add_to_registry(Call* call) {
    if (!call) return -1;
    
    Call* existing = call_find_by_id(call->call_id);
    if (existing != NULL) return -2;
    
    HASH_ADD_INT(calls, call_id, call);
    return 0;
}

static void call_remove_from_registry(Call* call) {
    if (!call) return;
    
    Call* found = call_find_by_id(call->call_id);
    if (!found || found != call) return;
    
    HASH_DEL(calls, call);
}

static int call_add_participant_to_array(Call* call, Connection* participant) {
    if (!call || !participant) return -1;
    return DENSE_ARRAY_ADD(call->participants, MAX_CALL_PARTICIPANTS, participant);
}

static int call_remove_participant_from_array(Call* call, Connection* participant) {
    if (!call || !participant) return -1;
    return DENSE_ARRAY_REMOVE(call->participants, MAX_CALL_PARTICIPANTS, participant);
}

static bool call_is_participant_in_array(const Call* call, const Connection* participant) {
    if (!call || !participant) return false;
    return DENSE_ARRAY_CONTAINS(call->participants, MAX_CALL_PARTICIPANTS, participant);
}

static int call_add_stream_to_array(Call* call, Stream* stream) {
    if (!call || !stream) return -1;
    return DENSE_ARRAY_ADD(call->streams, MAX_CALL_STREAMS, stream);
}

static int call_remove_stream_from_array(Call* call, Stream* stream) {
    if (!call || !stream) return -1;
    return DENSE_ARRAY_REMOVE(call->streams, MAX_CALL_STREAMS, stream);
}

static bool call_is_stream_in_array(const Call* call, const Stream* stream) {
    if (!call || !stream) return false;
    return DENSE_ARRAY_CONTAINS(call->streams, MAX_CALL_STREAMS, stream);
}

int call_remove_participant_safe(Call* call, Connection* participant) {
    if (!call || !participant) return 0; // Идемпотентность
    
    // Находим индекс и устанавливаем в NULL
    int index = DENSE_ARRAY_INDEX_OF(call->participants, MAX_CALL_PARTICIPANTS, participant);
    if (index >= 0) {
        call->participants[index] = NULL;
        return 0;
    }
    return -1;
}


/* Публичные функции */

Call* call_new(uint32_t call_id) {
    // Если call_id == 0, генерируем новый ID
    if (call_id == 0) {
        call_id = call_generate_id();
    } else {
        // Проверяем, не занят ли указанный ID
        if (call_find_by_id(call_id) != NULL) {
            fprintf(stderr, "Call ID %u is already in use\n", call_id);
            return NULL;
        }
    }
    
    Call* call = call_alloc(call_id);
    if (!call) {
        fprintf(stderr, "Failed to allocate call\n");
        return NULL;
    }
    
    // Добавляем в реестр
    if (call_add_to_registry(call) != 0) {
        fprintf(stderr, "Failed to add call %u to registry\n", call_id);
        call_free(call);
        return NULL;
    }
    
    printf("Created call %u\n", call_id);
    
    return call;
}

void call_delete(Call* call) {
    if (!call) return;
    
    printf("Destroying call %u\n", call->call_id);

    // 1. Удаляем всех участников из звонка используя безопасное удаление
    for (int i = MAX_CALL_PARTICIPANTS - 1; i >= 0; i--) {
        if (call->participants[i] != NULL) {
            Connection* participant = call->participants[i];
            // Используем безопасное удаление с обеих сторон
            call_remove_participant_safe(call, participant);
            connection_remove_call_safe(participant, call);
        }
    }

    // 2. Отсоединяем стримы от звонка
    for (int i = MAX_CALL_STREAMS - 1; i >= 0; i--) {
        if (call->streams[i] != NULL) {
            Stream* stream = call->streams[i];
            call_remove_stream(call, stream);
        }
    }

    // 3. Удаляем из реестра и освобождаем память
    call_remove_from_registry(call);
    call_free(call);
}

int call_add_participant(Call* call, Connection* participant) {
    if (!call || !participant) return -1;
    

    if (!call_can_add_participant(call)) {
        fprintf(stderr, "Call %u has no space for new participants (MAX_CALL_PARTICIPANTS=%d)\n", 
                call->call_id, MAX_CALL_PARTICIPANTS);
        return -5;
    }
    
    if (!connection_can_add_call(participant)) {
        fprintf(stderr, "Connection %d has no space for new calls (MAX_CONNECTION_CALLS=%d)\n", 
                participant->fd, MAX_CONNECTION_CALLS);
        return -6;
    }

    // Проверяем, не является ли уже участником
    if (call_has_participant(call, participant)) {
        return -2;
    }
    
    // Добавляем в массив участников звонка
    if (call_add_participant_to_array(call, participant) != 0) {
        return -3;
    }
    
    // Добавляем звонок в calls участника
    if (connection_add_call(participant, call) != 0) {
        // Откатываем добавление в массив участников
        call_remove_participant_from_array(call, participant);
        return -4;
    }
    
    printf("Added participant fd=%d to call %u\n", participant->fd, call->call_id);
    
    return 0;
}

int call_remove_participant(Call* call, Connection* participant) {
    if (!call || !participant) return 0; // Идемпотентность
    
    // Удаляем из массива участников звонка (безопасно)
    call_remove_participant_safe(call, participant);
    
    // Удаляем звонок из calls участника (безопасно)
    connection_remove_call_safe(participant, call);
    
    printf("Removed participant %s from call %u\n", 
           connection_get_address_string(participant), call->call_id);
    
    return 0;
}


bool call_has_participant(const Call* call, const Connection* participant) {
    return call_is_participant_in_array(call, participant);
}

int call_get_participant_count(const Call* call) {
    if (!call) return 0;
    return (int)DENSE_ARRAY_COUNT(call->participants, MAX_CALL_PARTICIPANTS);
}

int call_add_stream(Call* call, Stream* stream) {
    if (!call || !stream) return -1;
    
    // Проверяем, не является ли уже стримом звонка
    if (call_has_stream(call, stream)) {
        printf("Stream %u already in call %u\n", stream->stream_id, call->call_id);
        return -2;
    }
    
    // Проверяем лимит стримов
    if (call_get_stream_count(call) >= MAX_CALL_STREAMS) {
        printf("Call %u has reached maximum streams limit\n", call->call_id);
        return -3;
    }
    
    // Добавляем в массив стримов звонка
    if (call_add_stream_to_array(call, stream) != 0) {
        printf("Failed to add stream %u to call %u array\n", stream->stream_id, call->call_id);
        return -4;
    }
    
    // Устанавливаем call для стрима
    stream->call = call;
    
    printf("Added stream %u to call %u\n", stream->stream_id, call->call_id);
    
    return 0;
}

int call_remove_stream(Call* call, Stream* stream) {
    if (!call || !stream) return -1;
    
    // Удаляем из массива стримов звонка
    if (call_remove_stream_from_array(call, stream) != 0) {
        return -2;
    }
    
    // Снимаем call со стрима
    stream->call = NULL;
    
    printf("Removed stream %u from call %u\n", stream->stream_id, call->call_id);
    
    return 0;
}

bool call_has_stream(const Call* call, const Stream* stream) {
    return call_is_stream_in_array(call, stream);
}

int call_get_stream_count(const Call* call) {
    if (!call) return 0;
    return (int)DENSE_ARRAY_COUNT(call->streams, MAX_CALL_STREAMS);
}

Call* call_find_by_id(uint32_t call_id) {
    Call* call = NULL;
    HASH_FIND_INT(calls, &call_id, call);
    return call;
}

int call_find_by_participant(const Connection* participant, Call** result, int max_results) {
    if (!participant || !result || max_results <= 0) return 0;
    
    int count = 0;
    Call* current, *tmp;
    
    HASH_ITER(hh, calls, current, tmp) {
        if (call_has_participant(current, participant)) {
            result[count++] = current;
            if (count >= max_results) break;
        }
    }
    return count;
}

Connection* call_find_participant_by_id(const Call* call, uint32_t connection_id) {
    if (!call) return NULL;
    
    for (int i = 0; i < MAX_CALL_PARTICIPANTS; ++i) {
        if (call->participants[i] != NULL && call->participants[i]->fd == (int)connection_id) {
            return call->participants[i];
        }
    }
    
    return NULL;
}

Stream* call_find_stream_by_id(const Call* call, uint32_t stream_id) {
    if (!call) return NULL;
    
    for (int i = 0; i < MAX_CALL_STREAMS; ++i) {
        if (call->streams[i] != NULL && call->streams[i]->stream_id == stream_id) {
            return call->streams[i];
        }
    }
    
    return NULL;
}

bool call_can_add_participant(const Call* call) {
    if (!call) return false;
    
    for (int i = 0; i < MAX_CALL_PARTICIPANTS; i++) {
        if (call->participants[i] == NULL) {
            return true;
        }
    }
    return false;
}

bool call_can_add_stream(const Call* call) {
    if (!call) return false;
    
    for (int i = 0; i < MAX_CALL_STREAMS; i++) {
        if (call->streams[i] == NULL) {
            return true;
        }
    }
    return false;
}