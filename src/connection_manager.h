#pragma once

#include "protocol.h"
#include "nonblocking_ssl.h"

typedef struct Connection {
    int client_fd;
    SSL *ssl;
    struct NBSSL_Buffer ssl_in;
    struct NBSSL_Buffer ssl_out;
    int flags;
    int status;
    struct Call *call;
    struct sockaddr_in udp_addr;
} Connection;

// Управление соединениями
void Connection_construct(Connection* self);
void Connection_destruct(Connection* self);
void Connection_add(Connection* elem);
Connection* Connection_find(int client_fd);
void Connection_delete(Connection* connection);
void Connection_delete_with_disconnectfromcalls(Connection* self);

// Обработчики состояний
int handle_client(int fd);
int handle_unfinished_handshake(Connection *c);
int handle_new_command(Connection *c);

// Обработчики клиентских команд
int handle_info_client(Connection *c);
int handle_callstart_client(Connection *c);
int handle_callend_client(Connection *c);
int handle_calljoin_client(Connection *c);
int handle_callleave_client(Connection *c);
int handle_errorunknown_client(Connection *c);

// Обработчики серверных ответов  
int handle_callstart_server(Connection *c);
int handle_callend_server(Connection *c);
int handle_calljoin_server(Connection *c);
int handle_callleave_server(Connection *c);
int handle_callleaveowner_server(Connection *c);
int handle_errorunknown_server(Connection *c);
int handle_callnewmember_server(Connection *c);
