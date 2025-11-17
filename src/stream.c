#include "stream.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

Stream* streams = NULL;

Stream* stream_alloc(uint32_t stream_id, Connection* owner) {
    Stream* s = (Stream*)malloc(sizeof(Stream));
    if (!s) return NULL;
    
    s->stream_id = stream_id;
    s->owner = owner;
    s->recipient_count = 0;
    for (int i = 0; i < STREAM_MAX_RECIPIENTS; ++i) 
        s->recipients[i] = NULL;
    
    return s;
}

void stream_free(Stream* stream) {
    if (!stream) return;
    free(stream);
}

/* Управление получателями (только массив) */
int stream_add_recipient_to_array(Stream* stream, Connection* recipient) {
    if (!stream || !recipient) return -1;
    if (stream->recipient_count >= STREAM_MAX_RECIPIENTS) return -2;
    if (stream_is_recipient_in_array(stream, recipient)) return -3;
    
    stream->recipients[stream->recipient_count++] = recipient;
    return 0;
}

int stream_remove_recipient_from_array(Stream* stream, Connection* recipient) {
    if (!stream || !recipient) return -1;
    
    for (uint32_t i = 0; i < stream->recipient_count; ++i) {
        if (stream->recipients[i] == recipient) {
            // Плотный массив: сдвигаем элементы
            for (uint32_t j = i; j + 1 < stream->recipient_count; ++j) {
                stream->recipients[j] = stream->recipients[j + 1];
            }
            stream->recipients[stream->recipient_count - 1] = NULL;
            stream->recipient_count--;
            return 0;
        }
    }
    return -2;
}

int stream_is_recipient_in_array(const Stream* stream, const Connection* recipient) {
    if (!stream || !recipient) return 0;
    for (uint32_t i = 0; i < stream->recipient_count; ++i) {
        if (stream->recipients[i] == recipient) return 1;
    }
    return 0;
}

void stream_clear_recipients_array(Stream* stream) {
    if (!stream) return;
    for (uint32_t i = 0; i < STREAM_MAX_RECIPIENTS; ++i) 
        stream->recipients[i] = NULL;
    stream->recipient_count = 0;
}

/* Реестр стримов */
int stream_add_to_registry(Stream* stream) {
    if (!stream) return -1;
    
    Stream* existing = stream_find_by_id(stream->stream_id);
    if (existing != NULL) return -2;
    
    HASH_ADD_INT(streams, stream_id, stream);
    return 0;
}

int stream_remove_from_registry(Stream* stream) {
    if (!stream) return -1;
    
    Stream* found = stream_find_by_id(stream->stream_id);
    if (!found || found != stream) return -2;
    
    HASH_DEL(streams, stream);
    return 0;
}

Stream* stream_find_by_id(uint32_t stream_id) {
    Stream* s = NULL;
    HASH_FIND_INT(streams, &stream_id, s);
    return s;
}

/* Поиск */
int stream_find_by_owner_in_registry(const Connection* owner, Stream** result, int max_results) {
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

int stream_find_by_recipient_in_registry(const Connection* recipient, Stream** result, int max_results) {
    if (!recipient || !result || max_results <= 0) return 0;
    
    int count = 0;
    Stream* current, *tmp;
    
    HASH_ITER(hh, streams, current, tmp) {
        if (stream_is_recipient_in_array(current, recipient)) {
            result[count++] = current;
            if (count >= max_results) break;
        }
    }
    return count;
}

/* Генерация ID */
uint32_t stream_generate_id(void) {
    static int initialized = 0;
    if (!initialized) {
        srand((unsigned int)time(NULL));
        initialized = 1;
    }
    
    uint32_t id;
    do {
        id = (uint32_t)rand()%308915775;
    } while (stream_find_by_id(id) != NULL && id != 0);
    
    return id;
}