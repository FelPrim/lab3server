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
volatile sig_atomic_t stop_requested = 0;
volatile sig_atomic_t reload_requested = 0;
char stop_server = 0;

void calling_stop(int signo){
    stop_requested = 1;
    stop_server = 1;
}

void calling_reload(int signo){
    reload_requested = 1;
    stop_server = 1;
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

int Queue_append(struct Queue* self, TASK* elem){
    assert(self);
    if (unlikely(self->count >= self->size)){
        fprintf(stderr, "%d\n", self->size);
        uint32_t right_part = self->size - self->shift;
        // 4567123
        // 0123456 (uintptr_t) first - (uintptr_t) mem   
        uint32_t new_sz = self->size*2;
        void* new_mem = malloc(new_sz*sizeof(TASK));
        if (new_mem == NULL){
            perror("[malloc]");
            return EXIT_FAILURE;
        }
        //if (likely(shift != 0)){
        //    // 2345671 - особого случая нет
        //    memcpy(new_mem, self->first, sizeof(TASK)*(right_part));
        //    memcpy(new_mem+right_part, mem, sizeof(TASK)*shift);
        //}
        memcpy(new_mem, (TASK*)self->mem + self->shift, sizeof(TASK)*right_part);
        if (likely(self->shift != 0)){
            memcpy((TASK*)new_mem+right_part, self->mem, sizeof(TASK)*self->shift);
        }
        free(self->mem);
        self->mem = new_mem;
        self->shift = 0;
        self->size = new_sz;
    }
    uint32_t index = (self->shift+self->count)%self->size;
    ((TASK*) self->mem)[index] = *elem;
    self->count++;
    return 0;
} 

void Queue_shift(struct Queue* self){
    assert(self);
    assert(self->count > 0);
    self->shift = (self->shift + 1) % self->size;
    self->count--;
}

int Queue_construct(struct Queue* self, uint32_t size){
    assert(self);
    self->size = size;
    self->shift = 0;
    self->count = 0;
    self->mem = malloc(self->size*sizeof(TASK));
    if (self->mem == NULL){
        perror("[malloc]");
        return EXIT_FAILURE;
    }
    return 0;
}
#define TASKS_STARTINGSIZE 32

struct Queue task_queue;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  queue_cond  = PTHREAD_COND_INITIALIZER;

void do_task(TASK* task){
    printf("i: %d\n", *task);
    // TODO implement
}

void* db_thread_fn(void* args){
    assert(task_queue.mem);
    while(1){
        pthread_mutex_lock(&queue_mutex);
        while (task_queue.count == 0 && !stop_server){
            pthread_cond_wait(&queue_cond, &queue_mutex);
            // Если мы зашли в эту функцию, то выйдем только после того как где-то будет выполнен pthread_cond_signal(&queue_cond);
        }
        if (task_queue.count == 0 && stop_server){
            // выход
            Queue_destruct(&task_queue);
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        // task_queue.count > 0
        TASK *task = (TASK*) task_queue.mem + task_queue.shift;
        Queue_shift(&task_queue);
        pthread_mutex_unlock(&queue_mutex);
        
        do_task(task);
    }
}

int enqueue_task(TASK* task){
    pthread_mutex_lock(&queue_mutex);
    int status = Queue_append(&task_queue, task);
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
    return status;
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
    
    SERVER_START:
    int status = Queue_construct(&task_queue, TASKS_STARTINGSIZE);
    if (status)
        return status;

    pthread_t db_thread;
    // дескриптор, атрибуты (default), указатели на: функцию, параметры
    int thread_status = pthread_create(&db_thread, NULL, db_thread_fn, NULL);
    if (thread_status != 0){
        fprintf(stderr, "pthread_create, status=%d\n", thread_status);
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    // db_thread_fn работает
    int i = 0;
    while (!stop_server){
        enqueue_task(&i);
        ++i;
    }
    
    // завершение работы
    pthread_mutex_lock(&queue_mutex);
    stop_server = true;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);

    pthread_join(db_thread, NULL);

    if (reload_requested)
        goto SERVER_START;

    return 0;
}
