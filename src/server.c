#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include "network_setup.h"
#include "connection_manager.h"
#include "call_manager.h"
#include "protocol.h"
#include "useful_stuff.h"

#define MAX_EVENTS 23

volatile sig_atomic_t stop_requested = 0;

void calling_stop(int signo){
    stop_requested = 1;
}

int main(){
    int status = 0;
    
    // Инициализация
    if ((status = simple_starting_actions()) != 0)
        goto cleanup;
    
    if ((status = init_ssl_ctx()) != 0)
        goto cleanup;
        
    if ((status = tcp_socket_configuration()) != 0)
        goto ssl_cleanup;
        
    if ((status = udp_socket_configuration()) != 0)
        goto ssl_cleanup;
        
    if ((status = epfd_configuration()) != 0)
        goto ssl_cleanup;

    // Главный цикл
    struct epoll_event events[MAX_EVENTS];
    while (!stop_requested){
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; ++i){
            int fd = events[i].data.fd;
            if (likely(fd == ufd))
                handle_udp_packet();
            else if (unlikely(fd == tfd))
                handle_listen();
            else 
                handle_client(fd);
        }
    }

ssl_cleanup:
    cleanup_ssl_ctx();
cleanup:
    MainState_destruct();
    return status;
}
