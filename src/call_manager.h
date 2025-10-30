#pragma once

#include "useful_stuff.h"
#include "uthash.h"

// Forward declaration вместо включения connection_manager.h
typedef struct Connection Connection;

#define CALL_MAXSZ 64
#define CALL_NAME_SZ 7
#define SYMM_KEY_LEN 32

typedef struct Call{
    char callname[CALL_NAME_SZ];
    Connection* cons[CALL_MAXSZ];
    int count;
    unsigned char symm_key[SYMM_KEY_LEN];
    UT_hash_handle hh;
} Call;

extern Call *calls;

void Call_construct(Call* self);
void Call_destruct(Call* self);
void Call_add(Call* elem);
Call* Call_find(char callname[CALL_NAME_SZ]);
void Call_delete(Call* call);

// Добавьте объявления дополнительных функций
Call* Call_find_by_connection(Connection* conn);
int Call_remove_connection(Call* call, Connection* conn);
int Call_add_connection(Call* call, Connection* conn);
Connection* Call_get_owner(Call* call);
int Call_is_owner(Call* call, Connection* conn);
int Call_get_participant_count(Call* call);
int Call_get_participants(Call* call, Connection* exclude, int* participants, int max_count);
void Call_cleanup_all();
void Call_print_stats();
