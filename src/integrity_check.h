#pragma once
#include "connection.h"
#include "stream.h" 
#include "call.h"
#include <stdio.h>

inline static bool check_all_integrity(void) {
    printf("=== Starting system integrity check ===\n");
    bool all_ok = true;
    int connection_count = 0;
    int stream_count = 0;
    int call_count = 0;

    // 1. Проходим по всем звонкам
    Call *call, *call_tmp;
    HASH_ITER(hh, calls, call, call_tmp) {
        call_count++;
        printf("Checking call %u...\n", call->call_id);
        
        // Проверяем участников звонка
        for (int i = 0; i < MAX_CALL_PARTICIPANTS; i++) {
            if (call->participants[i] != NULL) {
                Connection* participant = call->participants[i];
                printf("  OK: Call %u has participant %d\n", call->call_id, participant->fd);
                
                // Проверяем, что участник знает о звонке
                if (!connection_is_in_call(participant, call)) {
                    printf("  ERROR: Call %u has participant %d, but participant doesn't have call in calls array\n", 
                           call->call_id, participant->fd);
                    all_ok = false;
                }
            }
        }
        
        // Проверяем стримы звонка
        for (int i = 0; i < MAX_CALL_STREAMS; i++) {
            if (call->streams[i] != NULL) {
                Stream* stream = call->streams[i];
                printf("  OK: Call %u has stream %u\n", call->call_id, stream->stream_id);
                
                // Проверяем, что стрим знает о звонке
                if (stream->call != call) {
                    printf("  ERROR: Call %u has stream %u, but stream has different call\n", 
                           call->call_id, stream->stream_id);
                    all_ok = false;
                }
            }
        }
    }

    // 2. Проходим по всем стримам
    Stream *stream, *stream_tmp;
    HASH_ITER(hh, streams, stream, stream_tmp) {
        stream_count++;
        printf("Checking stream %u...\n", stream->stream_id);
        
        // Проверяем владельца
        if (stream->owner) {
            printf("  OK: Stream %u owned by connection %d\n", stream->stream_id, stream->owner->fd);
            
            // Проверяем, что владелец знает о стриме
            if (!connection_is_owning_stream(stream->owner, stream)) {
                printf("  ERROR: Stream %u owned by connection %d, but owner doesn't have stream in own_streams\n", 
                       stream->stream_id, stream->owner->fd);
                all_ok = false;
            }
        } else {
            printf("  ERROR: Stream %u has no owner\n", stream->stream_id);
            all_ok = false;
        }
        
        // Проверяем получателей
        for (int i = 0; i < STREAM_MAX_RECIPIENTS; i++) {
            if (stream->recipients[i] != NULL) {
                Connection* recipient = stream->recipients[i];
                printf("  OK: Stream %u has recipient %d\n", stream->stream_id, recipient->fd);
                
                // Проверяем, что получатель знает о стриме
                if (!connection_is_watching_stream(recipient, stream)) {
                    printf("  ERROR: Stream %u has recipient %d, but recipient doesn't have stream in watch_streams\n", 
                           stream->stream_id, recipient->fd);
                    all_ok = false;
                }
            }
        }
        
        // Проверяем call (если есть)
        if (stream->call) {
            printf("  OK: Stream %u is in call %u\n", stream->stream_id, stream->call->call_id);
            
            // Проверяем, что звонок знает о стриме
            if (!call_has_stream(stream->call, stream)) {
                printf("  ERROR: Stream %u is in call %u, but call doesn't have stream in streams array\n", 
                       stream->stream_id, stream->call->call_id);
                all_ok = false;
            }
        }
    }

    // 3. Проходим по всем соединениям
    Connection *conn, *conn_tmp;
    HASH_ITER(hh, connections, conn, conn_tmp) {
        connection_count++;
        printf("Checking connection %d...\n", conn->fd);
        
        // Проверяем owned streams
        for (int i = 0; i < MAX_OUTPUT; i++) {
            if (conn->own_streams[i] != NULL) {
                Stream* stream = conn->own_streams[i];
                printf("  OK: Connection %d owns stream %u\n", conn->fd, stream->stream_id);
                
                // Проверяем, что стрим знает о владельце
                if (stream->owner != conn) {
                    printf("  ERROR: Connection %d owns stream %u, but stream has different owner\n", 
                           conn->fd, stream->stream_id);
                    all_ok = false;
                }
            }
        }
        
        // Проверяем watched streams
        for (int i = 0; i < MAX_INPUT; i++) {
            if (conn->watch_streams[i] != NULL) {
                Stream* stream = conn->watch_streams[i];
                printf("  OK: Connection %d watches stream %u\n", conn->fd, stream->stream_id);
                
                // Проверяем, что стрим знает о получателе
                if (!stream_has_recipient(stream, conn)) {
                    printf("  ERROR: Connection %d watches stream %u, but stream doesn't have connection as recipient\n", 
                           conn->fd, stream->stream_id);
                    all_ok = false;
                }
            }
        }
        
        // Проверяем calls
        for (int i = 0; i < MAX_CONNECTION_CALLS; i++) {
            if (conn->calls[i] != NULL) {
                Call* call = conn->calls[i];
                printf("  OK: Connection %d is in call %u\n", conn->fd, call->call_id);
                
                // Проверяем, что звонок знает о участнике
                if (!call_has_participant(call, conn)) {
                    printf("  ERROR: Connection %d is in call %u, but call doesn't have connection as participant\n", 
                           conn->fd, call->call_id);
                    all_ok = false;
                }
            }
        }
    }

    printf("=== Integrity check summary ===\n");
    printf("Connections: %d, Streams: %d, Calls: %d\n", connection_count, stream_count, call_count);
    printf("=== Integrity check %s ===\n", all_ok ? "PASSED" : "FAILED");
    
    return all_ok;
}