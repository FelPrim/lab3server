#pragma once
#include "connection.h"
#include "stream_logic.h" 

/* Базовые операции connection */
void connection_detach_from_streams(Connection* conn);
void connection_delete_owned_streams(Connection* conn);
void connection_close(Connection* conn);       
void connection_free(Connection* conn);        
void connection_destroy_full(Connection* conn);

/* Операции со стримами */
int connection_logic_add_own_stream(Connection* conn, Stream* stream);
int connection_logic_remove_own_stream(Connection* conn, Stream* stream);
int connection_logic_add_watch_stream(Connection* conn, Stream* stream);
int connection_logic_remove_watch_stream(Connection* conn, Stream* stream);