#pragma once
#include "stream.h"
#include "connection_logic.h"  // Добавлено для connection_logic функций
#include "connection.h"        // Добавлено для connection_get_address_string

/* Высокоуровневые операции со стримами */
Stream* stream_create(uint32_t stream_id, Connection* owner);
void stream_destroy(Stream* stream);

/* Управление получателями с поддержанием целостности */
int stream_add_recipient(Stream* stream, Connection* recipient);
int stream_remove_recipient(Stream* stream, Connection* recipient);
void stream_clear_recipients(Stream* stream);  // Добавлено объявление

/* Поиск (удобные обертки) */
int stream_find_by_owner(const Connection* owner, Stream** result, int max_results);
int stream_find_by_recipient(const Connection* recipient, Stream** result, int max_results);