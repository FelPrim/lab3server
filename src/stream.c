#include "stream.h"
#include "id_utils.h"
#include "connection.h"
#include "call.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

Stream* streams = NULL;

/* Внутренние функции */
static Stream* stream_alloc(uint32_t stream_id, Connection* owner, Call* call) {
    Stream* s = malloc(sizeof(Stream));
    if (!s) return NULL;
    
    // Теперь можно напрямую присваивать
    s->stream_id = stream_id;
    s->owner = owner;
    s->call = call;
    DENSE_ARRAY_INIT(s->recipients, STREAM_MAX_RECIPIENTS);
    
    return s;
}

static void stream_free(Stream* stream) {
    if (!stream) return;
    free(stream);
}

static int stream_add_to_registry(Stream* stream) {
    if (!stream) return -1;
    
    Stream* existing = stream_find_by_id(stream->stream_id);
    if (existing != NULL) return -2;
    
    HASH_ADD_INT(streams, stream_id, stream);
    return 0;
}

static void stream_remove_from_registry(Stream* stream) {
    if (!stream) return;
    
    Stream* found = stream_find_by_id(stream->stream_id);
    if (!found || found != stream) return;
    
    HASH_DEL(streams, stream);
}

static int stream_add_recipient_to_array(Stream* stream, Connection* recipient) {
    if (!stream || !recipient) return -1;
    return DENSE_ARRAY_ADD(stream->recipients, STREAM_MAX_RECIPIENTS, recipient);
}

static int stream_remove_recipient_from_array(Stream* stream, Connection* recipient) {
    if (!stream || !recipient) return -1;
    return DENSE_ARRAY_REMOVE(stream->recipients, STREAM_MAX_RECIPIENTS, recipient);
}

static bool stream_is_recipient_in_array(const Stream* stream, const Connection* recipient) {
    if (!stream || !recipient) return false;
    return DENSE_ARRAY_CONTAINS(stream->recipients, STREAM_MAX_RECIPIENTS, recipient);
}
/* Публичные функции */

Stream* stream_new(uint32_t stream_id, Connection* owner, Call* call) {
    if (!owner) {
        fprintf(stderr, "Stream owner cannot be NULL\n");
        return NULL;
    }
    
    // Если stream_id == 0, генерируем новый ID
    if (stream_id == 0) {
        stream_id = generate_id();
    } else {
        // Проверяем, не занят ли указанный ID
        if (stream_find_by_id(stream_id) != NULL) {
            fprintf(stderr, "Stream ID %u is already in use\n", stream_id);
            return NULL;
        }
    }

    if (!connection_can_add_own_stream(owner)) {
        fprintf(stderr, "Owner %d has no space for new streams (MAX_OUTPUT=%d)\n", 
                owner->fd, MAX_OUTPUT);
        return NULL;
    }
    
    // Дополнительная проверка для приватных стримов
    if (call != NULL) {
        // Проверяем, что владелец является участником звонка
        if (!call_has_participant(call, owner)) {
            fprintf(stderr, "Owner %d is not a participant of call %u\n", owner->fd, call->call_id);
            return NULL;
        }
        
        // Проверяем, не превышен ли лимит стримов в звонке
        if (call_get_stream_count(call) >= MAX_CALL_STREAMS) {
            fprintf(stderr, "Call %u has reached maximum streams limit\n", call->call_id);
            return NULL;
        }

    }
    
    Stream* stream = stream_alloc(stream_id, owner, call);
    if (!stream) {
        fprintf(stderr, "Failed to allocate stream\n");
        return NULL;
    }
    
    // Добавляем в реестр
    if (stream_add_to_registry(stream) != 0) {
        fprintf(stderr, "Failed to add stream %u to registry\n", stream_id);
        stream_free(stream);
        return NULL;
    }
    
    // Добавляем к владельцу
    if (connection_add_own_stream(owner, stream) != 0) {
        fprintf(stderr, "Failed to add stream %u to owner %d\n", stream_id, owner->fd);
        stream_remove_from_registry(stream);
        stream_free(stream);
        return NULL;
    }
    
    // Если стрим приватный (связан с call), добавляем его в call
    if (call != NULL) {
        if (call_add_stream(call, stream) != 0) {
            fprintf(stderr, "Failed to add stream %u to call %u\n", stream_id, call->call_id);
            connection_remove_own_stream(owner, stream);
            stream_remove_from_registry(stream);
            stream_free(stream);
            return NULL;
        }
    }
    
    printf("Created stream %u for owner %s (call: %s)\n", 
           stream_id, connection_get_address_string(owner),
           call ? "private" : "public");
    
    return stream;
}

void stream_delete(Stream* stream) {
    if (!stream) return;
    
    printf("Destroying stream %u\n", stream->stream_id);
    
    // 1. Удаляем stream из всех recipients (зрителей)
    for (int i = 0; i < STREAM_MAX_RECIPIENTS; i++) {
        if (stream->recipients[STREAM_MAX_RECIPIENTS - 1 - i] != NULL) {
            Connection* recipient = stream->recipients[STREAM_MAX_RECIPIENTS - 1 - i];
            connection_remove_watch_stream(recipient, stream);
        }
    }
    
    // 2. У владельца убираем из own_streams
    if (stream->owner) {
        connection_remove_own_stream(stream->owner, stream);
    }
    
    // 3. Если стрим приватный, убираем его из call
    if (stream->call != NULL) {
        call_remove_stream(stream->call, stream);
    }
    
    // 4. Удаляем из реестра и освобождаем память
    stream_remove_from_registry(stream);
    stream_free(stream);
}

int stream_add_recipient(Stream* stream, Connection* recipient) {
    if (!stream || !recipient) return -1;
    
    if (!stream_can_add_recipient(stream)) {
        fprintf(stderr, "Stream %u has no space for new recipients (STREAM_MAX_RECIPIENTS=%d)\n",
                stream->stream_id, STREAM_MAX_RECIPIENTS);
        return -6;
    }
    
    // ПРОВЕРКА: Есть ли место у получателя для watch_streams?
    if (!connection_can_add_watch_stream(recipient)) {
        fprintf(stderr, "Connection %d has no space to watch new streams (MAX_INPUT=%d)\n",
                recipient->fd, MAX_INPUT);
        return -7;
    }

    // Проверяем, не является ли уже получателем
    if (stream_has_recipient(stream, recipient)) {
        return -2;
    }
    
    // Проверяем UDP-адрес получателя
    if (!connection_has_udp(recipient)) {
        return -3;
    }
    
    // Добавляем в массив получателей стрима
    if (stream_add_recipient_to_array(stream, recipient) != 0) {
        return -4;
    }
    
    // Добавляем стрим в watch_streams получателя
    if (connection_add_watch_stream(recipient, stream) != 0) {
        // Откатываем добавление в массив получателей
        stream_remove_recipient_from_array(stream, recipient);
        return -5;
    }
    
    printf("Added recipient %s to stream %u\n", 
           connection_get_address_string(recipient), stream->stream_id);
    
    return 0;
}

int stream_remove_recipient(Stream* stream, Connection* recipient) {
    if (!stream || !recipient) return -1;
    
    // Удаляем стрим из watch_streams получателя
    connection_remove_watch_stream(recipient, stream);
    
    // Удаляем из массива получателей стрима
    if (stream_remove_recipient_from_array(stream, recipient) != 0) {
        return -2;
    }
    
    printf("Removed recipient %s from stream %u\n", 
           connection_get_address_string(recipient), stream->stream_id);
    
    return 0;
}

bool stream_has_recipient(const Stream* stream, const Connection* recipient) {
    return stream_is_recipient_in_array(stream, recipient);
}

int stream_get_recipient_count(const Stream* stream) {
    if (!stream) return 0;
    return (int)DENSE_ARRAY_COUNT(stream->recipients, STREAM_MAX_RECIPIENTS);
}

Stream* stream_find_by_id(uint32_t stream_id) {
    Stream* s = NULL;
    HASH_FIND_INT(streams, &stream_id, s);
    return s;
}

int stream_find_by_owner(const Connection* owner, Stream** result, int max_results) {
    if (!owner || !result || max_results <= 0) return 0;
    
    int count = 0;
    Stream* current, *tmp;
    
    HASH_ITER(hh, streams, current, tmp) {
        if (current->owner == owner) {
            result[count++] = current;
            if (count >= max_results) break;
        }
    }
    return count;
}

int stream_find_by_recipient(const Connection* recipient, Stream** result, int max_results) {
    if (!recipient || !result || max_results <= 0) return 0;
    
    int count = 0;
    Stream* current, *tmp;
    
    HASH_ITER(hh, streams, current, tmp) {
        if (stream_has_recipient(current, recipient)) {
            result[count++] = current;
            if (count >= max_results) break;
        }
    }
    return count;
}

Call* stream_get_call(const Stream* stream) {
    return stream ? stream->call : NULL;
}

bool stream_is_private(const Stream* stream) {
    return stream && stream->call != NULL;
}
bool stream_can_add_recipient(const Stream* stream) {
    if (!stream) return false;
    
    for (int i = 0; i < STREAM_MAX_RECIPIENTS; i++) {
        if (stream->recipients[i] == NULL) {
            return true;
        }
    }
    return false;
}