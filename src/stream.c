#include "stream.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "connection.h"

/* Глобальная таблица стримов (ключ = stream_id) */
Stream* streams = NULL;

static Stream* stream_alloc(uint32_t stream_id, Connection* owner) {
    if (!owner) return NULL;
    Stream* s = (Stream*)malloc(sizeof(Stream));
    if (!s) return NULL;
    s->stream_id = stream_id;
    s->owner = owner;
    s->recipient_count = 0;
    for (int i = 0; i < STREAM_MAX_RECIPIENTS; ++i) s->recipients[i] = NULL;
    return s;
}

Stream* stream_create(uint32_t stream_id, Connection* owner) {
    if (!owner) return NULL;
    /* Проверяем, свободен ли ID */
    if (stream_find_by_id(stream_id) != NULL) return NULL;
    Stream* s = stream_alloc(stream_id, owner);
    if (!s) return NULL;
    if (stream_add_to_registry(s) != 0) {
        free(s);
        return NULL;
    }
    /* Добавляем стрим в список владельца (при ошибке аккуратно откатим) */
    if (connection_add_own_stream(owner, s) != 0) {
        stream_remove_from_registry(s);
        free(s);
        return NULL;
    }
    return s;
}

void stream_destroy(Stream* stream) {
    if (!stream) return;

    /* Убедимся, что поток всё ещё в реестре и совпадает по адресу
       — это предотвращает ситуации, когда кто-то уже удалил стрим вручную. */
    Stream* reg = stream_find_by_id(stream->stream_id);
    if (reg != stream) {
        /* Либо уже удалён, либо id занят другим объектом — безопасно ничего не делать. */
        return;
    }

    /* Удаляем из глобального реестра (hash) */
    stream_remove_from_registry(stream);

    /* Удаляем ссылку у владельца (если есть) */
    if (stream->owner) {
        connection_remove_own_stream(stream->owner, stream);
        stream->owner = NULL;
    }

    /* Удаляем всех получателей (каждый получатель удалит стрим из своего списка просмотров) */
    stream_clear_recipients(stream);

    /* Наконец освобождаем память */
    free(stream);
}

/* Управление получателями */
int stream_add_recipient(Stream* stream, Connection* recipient) {
    if (!stream || !recipient) return -1;
    /* Если уже есть — ничего не делаем */
    if (stream_is_recipient(stream, recipient)) return -2;
    if (stream->recipient_count >= STREAM_MAX_RECIPIENTS) return -3;
    stream->recipients[stream->recipient_count++] = recipient;
    return 0;
}

int stream_remove_recipient(Stream* stream, Connection* recipient) {
    if (!stream || !recipient) return -1;
    for (uint32_t i = 0; i < stream->recipient_count; ++i) {
        if (stream->recipients[i] == recipient) {
            /* Плотный массив: сдвигаем элементы влево */
            for (uint32_t j = i; j + 1 < stream->recipient_count; ++j) {
                stream->recipients[j] = stream->recipients[j + 1];
            }
            stream->recipients[stream->recipient_count - 1] = NULL;
            stream->recipient_count--;
            return 0;
        }
    }
    return -2; /* не найден */
}

int stream_is_recipient(const Stream* stream, const Connection* recipient) {
    if (!stream || !recipient) return 0;
    for (uint32_t i = 0; i < stream->recipient_count; ++i) {
        if (stream->recipients[i] == recipient) return 1;
    }
    return 0;
}

void stream_clear_recipients(Stream* stream) {
    if (!stream) return;
    /* Создаём копию, чтобы безопасно вызвать connection_remove_watch_stream */
    Connection* copy[STREAM_MAX_RECIPIENTS];
    uint32_t count = stream->recipient_count;
    for (uint32_t i = 0; i < count; ++i) copy[i] = stream->recipients[i];

    for (uint32_t i = 0; i < count; ++i) {
        Connection* r = copy[i];
        if (r) {
            connection_remove_watch_stream(r, stream);
        }
    }

    /* Очистим массив */
    for (uint32_t i = 0; i < STREAM_MAX_RECIPIENTS; ++i) stream->recipients[i] = NULL;
    stream->recipient_count = 0;
}

/* Реестр стримов */
int stream_add_to_registry(Stream* stream) {
    if (!stream) return -1;
    if (stream_find_by_id(stream->stream_id) != NULL) return -2;
    HASH_ADD_INT(streams, stream_id, stream);
    return 0;
}

int stream_remove_from_registry(Stream* stream) {
    if (!stream) return -1;
    Stream* found = stream_find_by_id(stream->stream_id);
    if (!found) return -2;
    if (found != stream) return -3;
    HASH_DEL(streams, stream);
    return 0;
}

Stream* stream_find_by_id(uint32_t stream_id) {
    Stream* s = NULL;
    HASH_FIND_INT(streams, &stream_id, s);
    return s;
}

/* Поиск стримов по владельцу или получателю */
int stream_find_by_owner(const Connection* owner, Stream** result, int max_results) {
    if (!owner || !result || max_results <= 0) return 0;
    int count = 0;
    Stream* itr;
    for (itr = streams; itr != NULL && count < max_results; itr = itr->hh.next) {
        if (itr->owner == owner) result[count++] = itr;
    }
    return count;
}

int stream_find_by_recipient(const Connection* recipient, Stream** result, int max_results) {
    if (!recipient || !result || max_results <= 0) return 0;
    int count = 0;
    Stream* itr;
    for (itr = streams; itr != NULL && count < max_results; itr = itr->hh.next) {
        if (stream_is_recipient(itr, recipient)) result[count++] = itr;
    }
    return count;
}

/* Простая генерация ID (не криптографическая) */
#include <time.h>
#include <stdlib.h>

uint32_t stream_generate_id(void) {
    static int initialized = 0;
    if (!initialized) {
        srand((unsigned int)time(NULL));
        initialized = 1;
    }
    const uint32_t max_id = 308915776u; /* 26^6 */
    return (uint32_t)(rand() % max_id);
}
