#include "stream_logic.h"
#include "stream.h"
#include "connection.h"        // Добавлено для connection_get_address_string
#include "connection_logic.h"  // Добавлено для connection_logic функций
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Высокоуровневые операции со стримами */

Stream* stream_create(uint32_t stream_id, Connection* owner) {
    // Если stream_id == 0, генерируем новый ID
    if (stream_id == 0) {
        stream_id = stream_generate_id();
    } else {
        // Проверяем, не занят ли указанный ID
        if (stream_find_by_id(stream_id) != NULL) {
            fprintf(stderr, "Stream ID %u is already in use\n", stream_id);
            return NULL;
        }
    }
    
    Stream* stream = stream_alloc(stream_id, owner);
    if (!stream) return NULL;
    
    // Добавляем в реестр
    if (stream_add_to_registry(stream) != 0) {
        stream_free(stream);
        return NULL;
    }
    
    // Добавляем к владельцу через connection_logic
    if (connection_logic_add_own_stream(owner, stream) != 0) {
        stream_remove_from_registry(stream);
        stream_free(stream);
        return NULL;
    }
    
    printf("Created stream %u for owner %s\n", 
           stream_id, connection_get_address_string(owner));
    
    return stream;
}

void stream_destroy(Stream* stream) {
    if (!stream) return;
    
    printf("Destroying stream %u\n", stream->stream_id);
    
    // Удаляем стрим из owned streams владельца
    if (stream->owner) {
        connection_logic_remove_own_stream(stream->owner, stream);
    }
    
    // Очищаем получателей
    stream_clear_recipients(stream);
    
    // Удаляем из реестра
    stream_remove_from_registry(stream);
    
    // Освобождаем память
    stream_free(stream);
}

/* Управление получателями с поддержанием целостности */

int stream_add_recipient(Stream* stream, Connection* recipient) {
    if (!stream || !recipient) return -1;
    
    // Проверяем, не является ли уже получателем
    if (stream_is_recipient_in_array(stream, recipient)) {
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
    if (connection_logic_add_watch_stream(recipient, stream) != 0) {
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
    connection_logic_remove_watch_stream(recipient, stream);
    
    // Удаляем из массива получателей стрима
    if (stream_remove_recipient_from_array(stream, recipient) != 0) {
        return -2;
    }
    
    printf("Removed recipient %s from stream %u\n", 
           connection_get_address_string(recipient), stream->stream_id);
    
    return 0;
}

void stream_clear_recipients(Stream* stream) {
    if (!stream) return;
    
    printf("Clearing %u recipients from stream %u\n", 
           stream->recipient_count, stream->stream_id);
    
    // Удаляем стрим из watch_streams всех получателей
    for (uint32_t i = 0; i < stream->recipient_count; i++) {
        if (stream->recipients[i]) {
            connection_logic_remove_watch_stream(stream->recipients[i], stream);
        }
    }
    
    // Очищаем массив получателей
    stream_clear_recipients_array(stream);
}

/* Поиск (удобные обертки) */

int stream_find_by_owner(const Connection* owner, Stream** result, int max_results) {
    return stream_find_by_owner_in_registry(owner, result, max_results);
}

int stream_find_by_recipient(const Connection* recipient, Stream** result, int max_results) {
    return stream_find_by_recipient_in_registry(recipient, result, max_results);
}