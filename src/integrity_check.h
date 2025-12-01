#pragma once
#include "connection.h"
#include "stream.h" 
#include "call.h"
#include <stdio.h>
#include <arpa/inet.h>

static void print_connection_details(const Connection* conn) {
    if (!conn) return;
    
    printf("Connection %d:\n", conn->fd);
    printf("  TCP Address: %s\n", connection_get_address_string(conn));
    
    // UDP address
    char udp_str[INET_ADDRSTRLEN + 10];
    if (connection_has_udp(conn)) {
        inet_ntop(AF_INET, &conn->udp_addr.sin_addr, udp_str, INET_ADDRSTRLEN);
        int udp_port = ntohs(conn->udp_addr.sin_port);
        printf("  UDP Address: %s:%d\n", udp_str, udp_port);
    } else {
        printf("  UDP Address: Not set\n");
    }
    
    printf("  UDP Handshake Complete: %s\n", 
           connection_is_udp_handshake_complete(conn) ? "true" : "false");
    
    // Watch streams
    printf("  Watch Streams: [");
    bool first = true;
    for (int i = 0; i < MAX_INPUT; i++) {
        if (conn->watch_streams[i]) {
            if (!first) printf(", ");
            printf("%u", conn->watch_streams[i]->stream_id);
            first = false;
        }
    }
    if (first) printf("None");
    printf("]\n");
    
    // Own streams
    printf("  Own Streams: [");
    first = true;
    for (int i = 0; i < MAX_OUTPUT; i++) {
        if (conn->own_streams[i]) {
            if (!first) printf(", ");
            printf("%u", conn->own_streams[i]->stream_id);
            first = false;
        }
    }
    if (first) printf("None");
    printf("]\n");
    
    // Calls
    printf("  Calls: [");
    first = true;
    for (int i = 0; i < MAX_CONNECTION_CALLS; i++) {
        if (conn->calls[i]) {
            if (!first) printf(", ");
            printf("%u", conn->calls[i]->call_id);
            first = false;
        }
    }
    if (first) printf("None");
    printf("]\n");
    
    // Buffer info
    printf("  Read Buffer: pos=%zu, expected_size=%zu\n", 
           conn->read_buffer.position, conn->read_buffer.expected_size);
    printf("  Write Buffer: pos=%zu, expected_size=%zu\n", 
           conn->write_buffer.position, conn->write_buffer.expected_size);
    printf("\n");
}

static void print_stream_details(const Stream* stream) {
    if (!stream) return;
    
    printf("Stream %u:\n", stream->stream_id);
    printf("  Owner: %d\n", stream->owner ? stream->owner->fd : -1);
    printf("  Call: %u\n", stream->call ? stream->call->call_id : 0);
    printf("  Is Private: %s\n", stream_is_private(stream) ? "true" : "false");
    
    // Recipients
    printf("  Recipients: [");
    bool first = true;
    for (int i = 0; i < STREAM_MAX_RECIPIENTS; i++) {
        if (stream->recipients[i]) {
            if (!first) printf(", ");
            printf("%d", stream->recipients[i]->fd);
            first = false;
        }
    }
    if (first) printf("None");
    printf("]\n");
    
    printf("  Recipient Count: %d\n", stream_get_recipient_count(stream));
    printf("  Can Add Recipient: %s\n", stream_can_add_recipient(stream) ? "true" : "false");
    printf("\n");
}

static void print_call_details(const Call* call) {
    if (!call) return;
    
    printf("Call %u:\n", call->call_id);
    
    // Participants
    printf("  Participants: [");
    bool first = true;
    for (int i = 0; i < MAX_CALL_PARTICIPANTS; i++) {
        if (call->participants[i]) {
            if (!first) printf(", ");
            printf("%d", call->participants[i]->fd);
            first = false;
        }
    }
    if (first) printf("None");
    printf("]\n");
    
    // Streams
    printf("  Streams: [");
    first = true;
    for (int i = 0; i < MAX_CALL_STREAMS; i++) {
        if (call->streams[i]) {
            if (!first) printf(", ");
            printf("%u", call->streams[i]->stream_id);
            first = false;
        }
    }
    if (first) printf("None");
    printf("]\n");
    
    printf("  Participant Count: %d\n", call_get_participant_count(call));
    printf("  Stream Count: %d\n", call_get_stream_count(call));
    printf("  Can Add Participant: %s\n", call_can_add_participant(call) ? "true" : "false");
    printf("  Can Add Stream: %s\n", call_can_add_stream(call) ? "true" : "false");
    printf("\n");
}

inline static bool check_all_integrity(void) {
    printf("=== Starting system integrity check ===\n");
    bool all_ok = true;
    int connection_count = 0;
    int stream_count = 0;
    int call_count = 0;

    // Собираем IDs для summary
    printf("=== Current Network State ===\n");
    
    // Connections
    Connection *conn, *conn_tmp;
    printf("Connections: [");
    HASH_ITER(hh, connections, conn, conn_tmp) {
        connection_count++;
        if (connection_count > 1) printf(", ");
        printf("%d", conn->fd);
    }
    if (connection_count == 0) printf("None");
    printf("]\n");
    
    // Streams
    Stream *stream, *stream_tmp;
    printf("Streams: [");
    HASH_ITER(hh, streams, stream, stream_tmp) {
        stream_count++;
        if (stream_count > 1) printf(", ");
        printf("%u", stream->stream_id);
    }
    if (stream_count == 0) printf("None");
    printf("]\n");
    
    // Calls
    Call *call, *call_tmp;
    printf("Calls: [");
    HASH_ITER(hh, calls, call, call_tmp) {
        call_count++;
        if (call_count > 1) printf(", ");
        printf("%u", call->call_id);
    }
    if (call_count == 0) printf("None");
    printf("]\n\n");
    
    // Подробная информация о каждом объекте
    printf("=== Detailed Object Information ===\n");
    
    // Connections details
    HASH_ITER(hh, connections, conn, conn_tmp) {
        print_connection_details(conn);
    }
    
    // Streams details
    HASH_ITER(hh, streams, stream, stream_tmp) {
        print_stream_details(stream);
    }
    
    // Calls details
    HASH_ITER(hh, calls, call, call_tmp) {
        print_call_details(call);
    }

    // Проверки целостности
    printf("=== Starting Integrity Verification ===\n");

    // 1. Проверяем звонки
    HASH_ITER(hh, calls, call, call_tmp) {
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

    // 2. Проверяем стримы
    HASH_ITER(hh, streams, stream, stream_tmp) {
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

    // 3. Проверяем соединения
    HASH_ITER(hh, connections, conn, conn_tmp) {
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
    printf("Objects: Connections=%d, Streams=%d, Calls=%d\n", connection_count, stream_count, call_count);
    printf("=== Integrity check %s ===\n", all_ok ? "PASSED" : "FAILED");
    
    return all_ok;
}