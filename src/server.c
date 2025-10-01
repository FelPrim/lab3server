#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include <signal.h>
#include <pthread.h>

//////////////////////////////////////////////////
// SIGNALS

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
volatile sig_atomic_t close_requested = 0;
volatile sig_atomic_t reload_requested = 0;

void calling_stop(int signo){
    stop_requested = 1;
}

void calling_reload(int signo){
    reload_requested = 1;
}

//////////////////////////////////////////////////
// PROTOCOL
#define TPORT 23230
#define UPORT 23231


//////////////////////////////////////////////////
// BD
// живёт на одном, своём потоке

typedef int TASK; // не int

// Удаляется самый старый элемент -> очередь 
// Потенциально неограниченное число элементов -> данные на куче
struct Queue {
    void* mem; // указатель на память, выделенную под очередь
    uint32_t size; // число, по которому можно понять размер выделенной памяти. Я возьму для этого максимальное число элементов
    //TASK* first; // указатель на первый элемент
    uint32_t shift; // first = mem + shift
    uint32_t count; // число элементов.
};
// # - Выделенная память
//  #######
//  45##123
void Queue_destruct(struct Queue* self){
    assert(self);
    free(self->mem);
}

void Queue_append(struct Queue* self, TASK* elem){
    assert(self);
    if (unlikely(self->count >= self->size)){
        uint32_t right_part = self->size - self->shift;
        // 4567123
        // 0123456 (uintptr_t) first - (uintptr_t) mem   
        uint32_t new_sz = size*2;
        void* new_mem = malloc(new_sz*sizeof(TASK));
        if (new_mem == NULL){
            perror("[malloc]");

        }
        //if (likely(shift != 0)){
        //    // 2345671 - особого случая нет
        //    memcpy(new_mem, self->first, sizeof(TASK)*(right_part));
        //    memcpy(new_mem+right_part, mem, sizeof(TASK)*shift);
        //}
        memcpy(new_mem, self->mem + self->shift, sizeof(TASK)*(right_part));
        if (likely(self->shift != 0)){
            memcpy(new_mem+right_part, mem, sizeof(TASK)*shift);
        }
        free(self->mem);
        self->mem = new_mem;
        self->shift = 0;
        self->size = new_sz;
    }
    uint32_t index = (shift+self->count)%self->size;
    ((TASK*) self->mem)[index] = TASK;
    self->count++;
} 

void Queue_shift(struct Queue* self){
    assert(self->count > 0);
    self->shift = (self->shift + 1) % self->size;
    self->count--;
}

//////////////////////////////////////////////////
// LOGIC


int main(){
    // собираю информацию об ошибках в error.log
    if (freopen("error.log", "w", stderr) == NULL){
        perror("freopen failed");
        exit(EXIT_FAILURE);
    }
    if (setvbuf(stderr, NULL, _IOLBF, 0) != 0) {
        perror("setvbuf failed");
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, calling_stop);
    signal(SIGTERM, calling_stop);
    signal(SIGHUP, calling_reload);
    // игнорирую сигнал связанный с записью в закрытый другой стороной сокет
    // signal(SIGPIPE, SIG_IGN);


    fflush(stderr);
    fclose(stderr);
    return 0;
}
