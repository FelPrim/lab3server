#include "call_manager.h"
#include "protocol.h"
#include "useful_stuff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <openssl/rand.h>

Call *calls = NULL;

void Call_construct(Call* self) {
    // Генерируем уникальное имя конференции
    static_assert(RAND_MAX == 2147483647, "26^6");
    int r = rand();
    for (int i = 0; i < CALL_NAME_SZ - 1; ++i) {
        self->callname[i] = r % 26 + 'A';
        r /= 26;
    }
    self->callname[CALL_NAME_SZ - 1] = '\0';
    
    // Инициализируем массив участников
    memset(self->cons, 0, sizeof(self->cons));
    self->count = 0;
    
    // Генерируем симметричный ключ
    if (RAND_priv_bytes(self->symm_key, SYMM_KEY_LEN) != 1) {
        fprintf(stderr, "Failed to generate symmetric key for call %s\n", self->callname);
        // В случае ошибки заполняем нулями
        memset(self->symm_key, 0, SYMM_KEY_LEN);
    }
    
    fprintf(stderr, "Call constructed: %s\n", self->callname);
}

void Call_destruct(Call* self) {
    // Безопасно очищаем ключ из памяти
    OPENSSL_cleanse(self->symm_key, SYMM_KEY_LEN);
    
    // Убираем ссылки на конференцию у всех участников
    for (int i = 0; i < self->count; ++i) {
        if (self->cons[i]) {
            self->cons[i]->call = NULL;
        }
    }
    
    fprintf(stderr, "Call destructed: %s\n", self->callname);
}

void Call_add(Call* elem) {
    HASH_ADD_STR(calls, callname, elem);
    fprintf(stderr, "Call added to hash table: %s\n", elem->callname);
}

Call* Call_find(char callname[CALL_NAME_SZ]) {
    Call* result;
    HASH_FIND_STR(calls, callname, result);
    return result;
}

void Call_delete(Call* call) {
    if (!call) return;
    
    fprintf(stderr, "Deleting call: %s\n", call->callname);
    
    HASH_DEL(calls, call);
    Call_destruct(call);
    free(call);
}

// Функция для поиска конференции по участнику
Call* Call_find_by_connection(Connection* conn) {
    Call *call, *tmp;
    
    HASH_ITER(hh, calls, call, tmp) {
        for (int i = 0; i < call->count; ++i) {
            if (call->cons[i] == conn) {
                return call;
            }
        }
    }
    return NULL;
}

// Функция для удаления участника из конференции
int Call_remove_connection(Call* call, Connection* conn) {
    if (!call || !conn) return -1;
    
    int found_index = -1;
    for (int i = 0; i < call->count; ++i) {
        if (call->cons[i] == conn) {
            found_index = i;
            break;
        }
    }
    
    if (found_index == -1) {
        return -1; // Участник не найден
    }
    
    // Сдвигаем массив
    for (int i = found_index; i < call->count - 1; ++i) {
        call->cons[i] = call->cons[i + 1];
    }
    call->count--;
    call->cons[call->count] = NULL;
    
    conn->call = NULL;
    
    fprintf(stderr, "Connection %d removed from call %s, remaining: %d\n", 
            conn->client_fd, call->callname, call->count);
    
    return 0;
}

// Функция для добавления участника в конференцию
int Call_add_connection(Call* call, Connection* conn) {
    if (!call || !conn) return -1;
    
    if (call->count >= CALL_MAXSZ) {
        fprintf(stderr, "Call %s is full, cannot add connection %d\n", 
                call->callname, conn->client_fd);
        return -1;
    }
    
    // Проверяем, не находится ли участник уже в конференции
    for (int i = 0; i < call->count; ++i) {
        if (call->cons[i] == conn) {
            fprintf(stderr, "Connection %d already in call %s\n", 
                    conn->client_fd, call->callname);
            return -1;
        }
    }
    
    call->cons[call->count] = conn;
    call->count++;
    conn->call = call;
    
    fprintf(stderr, "Connection %d added to call %s, total: %d\n", 
            conn->client_fd, call->callname, call->count);
    
    return 0;
}

// Функция для получения владельца конференции
Connection* Call_get_owner(Call* call) {
    if (!call || call->count == 0) return NULL;
    return call->cons[0];
}

// Функция для проверки, является ли соединение владельцем конференции
int Call_is_owner(Call* call, Connection* conn) {
    if (!call || !conn || call->count == 0) return 0;
    return call->cons[0] == conn;
}

// Функция для получения количества участников в конференции
int Call_get_participant_count(Call* call) {
    return call ? call->count : 0;
}

// Функция для получения списка участников (кроме указанного)
int Call_get_participants(Call* call, Connection* exclude, int* participants, int max_count) {
    if (!call || !participants || max_count <= 0) return 0;
    
    int count = 0;
    for (int i = 0; i < call->count && count < max_count; ++i) {
        if (call->cons[i] != exclude) {
            participants[count++] = call->cons[i]->client_fd;
        }
    }
    
    return count;
}

// Функция для очистки всех конференций (при завершении сервера)
void Call_cleanup_all() {
    Call *call, *tmp;
    
    HASH_ITER(hh, calls, call, tmp) {
        HASH_DEL(calls, call);
        Call_destruct(call);
        free(call);
    }
    
    fprintf(stderr, "All calls cleaned up\n");
}

// Функция для получения статистики по конференциям
void Call_print_stats() {
    int total_calls = HASH_COUNT(calls);
    int total_participants = 0;
    
    Call *call, *tmp;
    HASH_ITER(hh, calls, call, tmp) {
        total_participants += call->count;
        fprintf(stderr, "Call %s: %d participants\n", call->callname, call->count);
    }
    
    fprintf(stderr, "Total calls: %d, total participants: %d\n", 
            total_calls, total_participants);
}
