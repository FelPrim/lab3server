#ifndef NETWORK_SETUP_H
#define NETWORK_SETUP_H

#include <sys/epoll.h>
#include <openssl/ssl.h>
#include <netdb.h>
#include "connection_manager.h"

extern int epfd;
extern SSL_CTX* ssl_ctx;
extern int tfd;
extern int ufd;

int simple_starting_actions();
int init_ssl_ctx();
void cleanup_ssl_ctx();
int tcp_socket_configuration();
int udp_socket_configuration();
int epfd_configuration();
int handle_listen();
int handle_udp_packet();
void MainState_construct();
void MainState_destruct();

#endif // NETWORK_SETUP_H
