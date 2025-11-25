#pragma once

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include "connection.h"
#include "stream.h"
#include "call.h"

typedef struct {
    char message[1024];
    bool failed;
} TestContext;

#define TEST_INIT(ctx, test_name) do { \
    (ctx)->message[0] = '\0'; \
    (ctx)->failed = false; \
} while(0)

#define TEST_FAIL(ctx, fmt, ...) do { \
    snprintf((ctx)->message, sizeof((ctx)->message), "FAIL: " fmt, ##__VA_ARGS__); \
    (ctx)->failed = true; \
} while(0)

#define TEST_ASSERT(ctx, condition, fmt, ...) do { \
    if (!(condition)) { \
        TEST_FAIL(ctx, fmt, ##__VA_ARGS__); \
        return false; \
    } \
} while(0)

#define TEST_REPORT(ctx, test_name) do { \
    if ((ctx)->failed) { \
        printf("=== Test Failed: %s ===\n", test_name); \
        printf("%s\n", (ctx)->message); \
        return false; \
    } else { \
        printf("✓ %s passed\n", test_name); \
        return true; \
    } \
} while(0)

// test_common.h - упрощенная очистка
// В test_common.h - ИСПРАВЛЕННАЯ версия
static inline void cleanup_globals() {
    printf("=== Cleaning up globals ===\n");
    
    // Безопасный порядок очистки:
    
    // 1. Сначала удаляем все соединения (они удалят свои стримы)
    connection_close_all();
    
    // 2. Затем безопасно удаляем оставшиеся звонки
    Call *call, *call_tmp;
    HASH_ITER(hh, calls, call, call_tmp) {
        // Проверяем, что звонок еще валиден
        if (call && call->call_id != 0) {
            printf("Cleaning up orphaned call %u\n", call->call_id);
            
            // Безопасно удаляем участников
            for (int i = 0; i < MAX_CALL_PARTICIPANTS; i++) {
                if (call->participants[i] != NULL) {
                    Connection* participant = call->participants[i];
                    // Проверяем, что участник еще существует
                    if (connection_find(participant->fd) == participant) {
                        call_remove_participant(call, participant);
                    }
                    call->participants[i] = NULL;
                }
            }
            
            call_delete(call);
        }
    }
    
    // 3. Наконец безопасно удаляем оставшиеся стримы
    Stream *stream, *stream_tmp;
    HASH_ITER(hh, streams, stream, stream_tmp) {
        if (stream && stream->stream_id != 0) {
            printf("Cleaning up orphaned stream %u\n", stream->stream_id);
            
            // Безопасно очищаем связи
            if (stream->owner && connection_find(stream->owner->fd) == stream->owner) {
                connection_remove_own_stream(stream->owner, stream);
            }
            
            // Удаляем из реестра
            HASH_DEL(streams, stream);
            free(stream);
        }
    }
    
    printf("=== Global cleanup complete ===\n");
}