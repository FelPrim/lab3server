#include "connection_manager.h"
#include "call_manager.h"
#include "protocol.h"
#include "nonblocking_ssl.h"
#include "useful_stuff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <openssl/ssl.h>

Connection *connections = NULL;

#define DOING_NOTHING 0
#define SSL_ACCEPTING -1

void Connection_construct(Connection* self) {
    self->client_fd = 0;
    self->ssl = NULL;
    sslbuf_clean(&self->ssl_in);
    sslbuf_clean(&self->ssl_out);
    self->flags = UNINITIALIZED;
    self->status = DOING_NOTHING;
    self->call = NULL;
    memset(&self->udp_addr, 0, sizeof(self->udp_addr));
}

void Connection_destruct(Connection* self) {
    if (self->ssl) {
        SSL_free(self->ssl);
        self->ssl = NULL;
    }
    if (self->client_fd) {
        close(self->client_fd);
        self->client_fd = 0;
    }
    // Освобождаем буферы
    sslbuf_clean(&self->ssl_in);
    sslbuf_clean(&self->ssl_out);
}

void Connection_add(Connection* elem) {
    HASH_ADD_INT(connections, client_fd, elem);
}

Connection* Connection_find(int client_fd) {
    Connection* result;
    HASH_FIND_INT(connections, &client_fd, result);
    return result;
}

void Connection_delete(Connection* connection) {
    HASH_DEL(connections, connection);
    if (connection->client_fd) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, connection->client_fd, NULL);
    }
    Connection_destruct(connection);
    free(connection);
}

int handle_unfinished_handshake(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    int ret = NB_SSL_accept(ssl, fd, &c->flags);
    
    switch(ret) {
        case CAUGHT_ERROR: {
            Connection_delete(c);
            return -1;
        }
        case IS_COMPLETED: {
            c->status = DOING_NOTHING;
            fprintf(stderr, "SSL handshake completed for client %d\n", fd);
            break;
        }
        case ISNT_COMPLETED: {
            c->status = SSL_ACCEPTING;
            break;
        }
    }
    return ret;
}

int handle_new_command(Connection *c) {
    SSL *ssl = c->ssl;
    int fd = c->client_fd;
    unsigned char opcode;
    
    int n = SSL_read(ssl, &opcode, 1);
    if (n <= 0) {
        int ssl_err = SSL_get_error(ssl, n);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            return 1;
        }
        if (ssl_err == SSL_ERROR_WANT_WRITE) {
            change_flags(fd, &c->flags, SENDING);
            return 1;
        }
        Connection_delete_with_disconnectfromcalls(c);
        return -1;
    }

    fprintf(stderr, "Received command: %d from client %d\n", opcode, fd);
    
    switch (opcode) {
        case INFOCLIENT:
            c->status = INFOCLIENT;
            handle_info_client(c);
            break;
        case CALLSTARTCLIENT:
            c->status = CALLSTARTCLIENT;
            handle_callstart_client(c);
            break;
        case CALLENDCLIENT:
            c->status = CALLENDCLIENT;
            handle_callend_client(c);
            break;
        case CALLJOINCLIENT:
            c->status = CALLJOINCLIENT;
            handle_calljoin_client(c);
            break;
        case CALLLEAVECLIENT:
            c->status = CALLLEAVECLIENT;
            handle_callleave_client(c);
            break;
        case ERRORUNKNOWNCLIENT:
            c->status = ERRORUNKNOWNCLIENT;
            handle_errorunknown_client(c);
            break;
        default:
            fprintf(stderr, "Unknown command: %d from client %d\n", opcode, fd);
            unsigned char errbyte = ERRORUNKNOWNSERVER;
            SSL_write(ssl, &errbyte, 1);
            break;
    }
    return 0;
}

int handle_info_client(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    
    if (c->status != INFOCLIENT) {
        sslbuf_clean(buf);
        c->status = INFOCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR: {
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htonl((uint32_t) buf->seekend)
            };
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
        }
        case IS_COMPLETED: {
            uint16_t net_udp_port;
            sslbuf_read(buf, &net_udp_port, sizeof(uint16_t));
            c->udp_addr.sin_port = net_udp_port;  // Уже в сетевом порядке
            fprintf(stderr, "Client %d set UDP port: %d\n", fd, ntohs(net_udp_port));
            c->status = DOING_NOTHING;
            break;
        }
        case ISNT_COMPLETED:
            break;
    }
    return ret;
}

int handle_callstart_client(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    
    if (c->status != CALLSTARTCLIENT) {
        sslbuf_clean(buf);
        c->status = CALLSTARTCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR: {
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htonl((uint32_t) buf->seekend)
            };
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
        }
        case IS_COMPLETED: {
            // Клиент отправил пустое сообщение CALLSTARTCLIENT
            Call* call = calloc(1, sizeof(Call));
            Call_construct(call);
            
            struct CallStartInfo info = {
                .command = CALLSTARTSERVER,
                .creator_fd = c->client_fd  // Дескриптор не преобразуется
            };
            memcpy(info.callname, call->callname, CALL_NAME_SZ);
            memcpy(info.sym_key, call->symm_key, SYMM_KEY_LEN);
            
            sslbuf_clean(&c->ssl_out);
            sslbuf_write(&c->ssl_out, &info, sizeof(info));
            
            // Добавляем создателя в конференцию
            call->cons[0] = c;
            call->count = 1;
            c->call = call;
            
            Call_add(call);
            fprintf(stderr, "Call created: %s by client %d\n", call->callname, fd);
            
            return handle_callstart_server(c);
        }
        case ISNT_COMPLETED:
            break;
    }
    return ret;
}

int handle_callend_client(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    
    if (c->status != CALLENDCLIENT) {
        sslbuf_clean(buf);
        c->status = CALLENDCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR: {
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htonl((uint32_t) buf->seekend)
            };
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
        }
        case IS_COMPLETED: {
            char callname[CALL_NAME_SZ];
            sslbuf_read(buf, callname, CALL_NAME_SZ);
            
            Call* call = Call_find(callname);
            if (!call) {
                struct NBSSL_Buffer* buf2 = &c->ssl_out;
                sslbuf_clean(buf2);
                struct ErrorInfo info = {
                    .command = ERRORUNKNOWNSERVER,
                    .previous_command = c->status,
                    .size = htonl((uint32_t) buf->seekend)
                };
                memcpy(info.data, buf->data, buf->seekend);
                sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
                sslbuf_clean(buf);
                return handle_errorunknown_server(c);
            }
            
            // Проверяем, что клиент является создателем конференции
            if (call->cons[0] != c) {
                fprintf(stderr, "Client %d is not owner of call %s\n", fd, callname);
                // Отправляем ошибку - нет прав
                return -1;
            }
            
            // Уведомляем всех участников о завершении конференции
            for (int i = 0; i < call->count; ++i) {
                Connection* conn = call->cons[i];
                if (conn != c) {  // Не отправляем себе
                    struct NBSSL_Buffer *obuf = &conn->ssl_out;
                    sslbuf_clean(obuf);
                    char q = CALLENDSERVER;
                    sslbuf_write(obuf, &q, 1);
                    sslbuf_write(obuf, callname, CALL_NAME_SZ);
                    handle_callend_server(conn);
                }
                // Очищаем ссылку на конференцию
                conn->call = NULL;
            }
            
            Call_delete(call);
            c->status = DOING_NOTHING;
            fprintf(stderr, "Call %s ended by owner %d\n", callname, fd);
            break;
        }
        case ISNT_COMPLETED:
            break;
    }
    return ret;
}

int handle_calljoin_client(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    
    if (c->status != CALLJOINCLIENT) {
        sslbuf_clean(buf);
        c->status = CALLJOINCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR: {
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htonl((uint32_t) buf->seekend)
            };
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
        }
        case IS_COMPLETED: {
            char callname[CALL_NAME_SZ];
            sslbuf_read(buf, callname, CALL_NAME_SZ);
            
            Call* call = Call_find(callname);
            if (!call) {
                fprintf(stderr, "Call %s not found for client %d\n", callname, fd);
                // Отправляем ошибку - конференция не найдена
                return -1;
            }
            
            if (call->count >= CALL_MAXSZ) {
                fprintf(stderr, "Call %s is full, cannot join client %d\n", callname, fd);
                // Отправляем ошибку - конференция заполнена
                return -1;
            }
            
            // Уведомляем существующих участников о новом участнике
            for (int i = 0; i < call->count; ++i) {
                Connection *conn = call->cons[i];
                struct NBSSL_Buffer *obuf = &conn->ssl_out;
                struct CallMemberInfo info = {
                    .command = CALLNEWMEMBERSERVER,
                    .member_id = c->client_fd  // Дескриптор не преобразуется
                };
                memcpy(info.callname, callname, CALL_NAME_SZ);
                sslbuf_clean(obuf);
                sslbuf_write(obuf, &info, sizeof(info));
                handle_callnewmember_server(conn);
            }
            
            // Отправляем новому участнику информацию о конференции
            struct NBSSL_Buffer *obuf = &c->ssl_out;
            struct CallFullInfo info = {
                .command = CALLJOINSERVER,
                .joiner_fd = c->client_fd,  // Дескриптор не преобразуется
                .participants_count = (char)call->count  // char, не int
            };
            memcpy(info.callname, callname, CALL_NAME_SZ);
            memcpy(info.sym_key, call->symm_key, SYMM_KEY_LEN);
            
            // Заполняем список участников
            for (int i = 0; i < call->count; ++i) {
                info.participants[i] = call->cons[i]->client_fd;  // Дескрипторы не преобразуются
            }
            
            sslbuf_clean(obuf);
            size_t info_size = 1 + CALL_NAME_SZ + SYMM_KEY_LEN + sizeof(int) + 1 + call->count * sizeof(int);
            sslbuf_write(obuf, &info, info_size);
            
            // Добавляем клиента в конференцию
            call->cons[call->count] = c;
            call->count++;
            c->call = call;
            
            fprintf(stderr, "Client %d joined call %s\n", fd, callname);
            return handle_calljoin_server(c);
        }
        case ISNT_COMPLETED:
            break;
    }
    return ret;
}

int handle_callleave_client(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    
    if (c->status != CALLLEAVECLIENT) {
        sslbuf_clean(buf);
        c->status = CALLLEAVECLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR: {
            struct NBSSL_Buffer* buf2 = &c->ssl_out;
            sslbuf_clean(buf2);
            struct ErrorInfo info = {
                .command = ERRORUNKNOWNSERVER,
                .previous_command = c->status,
                .size = htonl((uint32_t) buf->seekend)
            };
            memcpy(info.data, buf->data, buf->seekend);
            sslbuf_write(buf2, &info, 1+1+sizeof(int)+buf->seekend);
            sslbuf_clean(buf);
            return handle_errorunknown_server(c);
        }
        case IS_COMPLETED: {
            char callname[CALL_NAME_SZ];
            sslbuf_read(buf, callname, CALL_NAME_SZ);
            
            Call* call = Call_find(callname);
            if (!call) {
                fprintf(stderr, "Call %s not found for client %d leave request\n", callname, fd);
                return -1;
            }
            
            // Удаляем клиента из конференции
            int found_index = -1;
            for (int i = 0; i < call->count; ++i) {
                if (call->cons[i] == c) {
                    found_index = i;
                    break;
                }
            }
            
            if (found_index == -1) {
                fprintf(stderr, "Client %d not in call %s\n", fd, callname);
                return -1;
            }
            
            // Сдвигаем массив
            for (int i = found_index; i < call->count - 1; ++i) {
                call->cons[i] = call->cons[i + 1];
            }
            call->count--;
            c->call = NULL;
            
            char remaining_count = (char)call->count;
            
            // Уведомляем остальных участников
            for (int i = 0; i < call->count; ++i) {
                Connection* conn = call->cons[i];
                struct NBSSL_Buffer *obuf = &conn->ssl_out;
                sslbuf_clean(obuf);
                
                if (call->count == 0) {
                    // Последний участник вышел - закрываем конференцию
                    char q = CALLLEAVEOWNERSERVER;
                    sslbuf_write(obuf, &q, 1);
                    sslbuf_write(obuf, callname, CALL_NAME_SZ);
                    handle_callleaveowner_server(conn);
                } else {
                    char q = CALLLEAVESERVER;
                    sslbuf_write(obuf, &q, 1);
                    sslbuf_write(obuf, callname, CALL_NAME_SZ);
                    sslbuf_write(obuf, &fd, sizeof(int));  // Дескриптор не преобразуется
                    sslbuf_write(obuf, &remaining_count, 1);
                    handle_callleave_server(conn);
                }
            }
            
            if (call->count == 0) {
                Call_delete(call);
            }
            
            c->status = DOING_NOTHING;
            fprintf(stderr, "Client %d left call %s\n", fd, callname);
            break;
        }
        case ISNT_COMPLETED:
            break;
    }
    return ret;
}

int handle_errorunknown_client(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_in;
    
    if (c->status != ERRORUNKNOWNCLIENT) {
        sslbuf_clean(buf);
        c->status = ERRORUNKNOWNCLIENT;
    }

    int ret = NB_SSL_read(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR:
            Connection_delete_with_disconnectfromcalls(c);
            break;
        case IS_COMPLETED:
            c->status = DOING_NOTHING;
            fprintf(stderr, "Error unknown client processed for client %d\n", fd);
            break;
        case ISNT_COMPLETED:
            break;
    }
    return ret;
}

// Реализации серверных обработчиков (отправка данных клиенту)
int handle_callstart_server(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_out;
    
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR:
            Connection_delete_with_disconnectfromcalls(c);
            break;
        case IS_COMPLETED:
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            fprintf(stderr, "Call start confirmation sent to client %d\n", fd);
            break;
        case ISNT_COMPLETED:
            c->status = CALLSTARTSERVER;
            break;
    }
    return ret;
}

int handle_callend_server(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_out;
    
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR:
            Connection_delete_with_disconnectfromcalls(c);
            break;
        case IS_COMPLETED:
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        case ISNT_COMPLETED:
            c->status = CALLENDSERVER;
            break;
    }
    return ret;
}

int handle_calljoin_server(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_out;
    
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR:
            Connection_delete_with_disconnectfromcalls(c);
            break;
        case IS_COMPLETED:
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        case ISNT_COMPLETED:
            c->status = CALLJOINSERVER;
            break;
    }
    return ret;
}

int handle_callleave_server(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_out;
    
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR:
            Connection_delete_with_disconnectfromcalls(c);
            break;
        case IS_COMPLETED:
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        case ISNT_COMPLETED:
            c->status = CALLLEAVESERVER;
            break;
    }
    return ret;
}

int handle_callleaveowner_server(Connection *c) {
    SSL *ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_out;
    
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR:
            Connection_delete_with_disconnectfromcalls(c);
            break;
        case IS_COMPLETED:
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        case ISNT_COMPLETED:
            c->status = CALLLEAVEOWNERSERVER;
            break;
    }
    return ret;
}

int handle_errorunknown_server(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_out;
    
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR:
            Connection_delete_with_disconnectfromcalls(c);
            break;
        case IS_COMPLETED:
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        case ISNT_COMPLETED:
            c->status = ERRORUNKNOWNSERVER;
            break;
    }
    return ret;
}

int handle_callnewmember_server(Connection *c) {
    SSL* ssl = c->ssl;
    int fd = c->client_fd;
    struct NBSSL_Buffer* buf = &c->ssl_out;
    
    int ret = NB_SSL_write(ssl, fd, &c->flags, buf);
    switch(ret) {
        case CAUGHT_ERROR:
            Connection_delete_with_disconnectfromcalls(c);
            break;
        case IS_COMPLETED:
            c->status = DOING_NOTHING;
            sslbuf_clean(buf);
            break;
        case ISNT_COMPLETED:
            c->status = CALLNEWMEMBERSERVER;
            break;
    }
    return ret;
}

int handle_client(int fd) {
    Connection *c = Connection_find(fd);
    if (!c) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
    }
    
    SSL *ssl = c->ssl;
    if (!ssl) {
        Connection_delete(c);
        return -1;
    }
    
    switch(c->status) {
        case DOING_NOTHING:
            return handle_new_command(c);
        case SSL_ACCEPTING:
            return handle_unfinished_handshake(c);
        case INFOCLIENT:
            return handle_info_client(c);
        case CALLSTARTCLIENT:
            return handle_callstart_client(c);
        case CALLENDCLIENT:
            return handle_callend_client(c);
        case CALLJOINCLIENT:
            return handle_calljoin_client(c);
        case CALLLEAVECLIENT:
            return handle_callleave_client(c);
        case ERRORUNKNOWNCLIENT:
            return handle_errorunknown_client(c);
        case ERRORUNKNOWNSERVER:
            return handle_errorunknown_server(c);
        case CALLSTARTSERVER:
            return handle_callstart_server(c);
        case CALLENDSERVER:
            return handle_callend_server(c);
        case CALLJOINSERVER:
            return handle_calljoin_server(c);
        case CALLNEWMEMBERSERVER:
            return handle_callnewmember_server(c);
        case CALLLEAVESERVER:
            return handle_callleave_server(c);
        case CALLLEAVEOWNERSERVER:
            return handle_callleaveowner_server(c);
        default:
            fprintf(stderr, "Unknown status %d for client %d\n", c->status, fd);
            return -1;
    }
}

void Connection_delete_with_disconnectfromcalls(Connection* self) {
    if (self->call) {
        Call* call = self->call;
        
        // Удаляем клиента из конференции
        int found_index = -1;
        for (int i = 0; i < call->count; ++i) {
            if (call->cons[i] == self) {
                found_index = i;
                break;
            }
        }
        
        if (found_index != -1) {
            // Сдвигаем массив
            for (int i = found_index; i < call->count - 1; ++i) {
                call->cons[i] = call->cons[i + 1];
            }
            call->count--;
            
            char remaining_count = (char)call->count;
            
            // Уведомляем остальных участников
            for (int i = 0; i < call->count; ++i) {
                Connection* conn = call->cons[i];
                struct NBSSL_Buffer *obuf = &conn->ssl_out;
                sslbuf_clean(obuf);
                
                if (found_index == 0 && call->count > 0) {
                    // Вышел создатель - назначаем нового
                    char q = CALLLEAVEOWNERSERVER;
                    sslbuf_write(obuf, &q, 1);
                    sslbuf_write(obuf, call->callname, CALL_NAME_SZ);
                    handle_callleaveowner_server(conn);
                } else {
                    char q = CALLLEAVESERVER;
                    sslbuf_write(obuf, &q, 1);
                    sslbuf_write(obuf, call->callname, CALL_NAME_SZ);
                    sslbuf_write(obuf, &self->client_fd, sizeof(int));
                    sslbuf_write(obuf, &remaining_count, 1);
                    handle_callleave_server(conn);
                }
            }
            
            if (call->count == 0) {
                Call_delete(call);
            }
        }
    }
    
    Connection_delete(self);
}
