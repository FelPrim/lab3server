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


#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

//////////////////////////////////////////////////
// SIGNALS


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

#define MAX_EVENTS 64


//////////////////////////////////////////////////
// INTERNET
#define TPORT 23230
#define UPORT 23231
#define TPSTR "23230"
#define UPSTR "23231"

int socket_configure(int *sock_fd, struct addrinfo *result){
    struct addrinfo *p;
    int oresult = 0;
    for (p = result, p != NULL; p=p->ai_next){
        if ((*sockfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) < 0){
            perror("socket");
            continue;
        }
		int opt = 1;
        if (setsockopt(*sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizoef(opt)) < 0){
            perror("setsockopt");
            continue;
        }
        if (bind(*sock_fd, result->ai_addr, result->ai_addrlen) < 0){
            perror("bind");
            oresult = 3;
            continue;
        }
		break;
    }
	
	freeaddrinfo(result);		
	int flags = fcntl(*sock_fd, F_GETFL, 0);
	if (flags < 0){
		perror("fcntl_getfl");
		oresult = 4;
		goto sc_end;
	}
	if (fcntl(*sock_fd, F_SETFL, flags | O_NONBLOCK) < 0){
		perror("fcntl_setfl");
		oresult = 5;
		goto sc_end;
	}
	sc_end:
	return oresult;
}

struct Connections{
    void* mem; // TODO понять что это
    int *socks;
    uint32_t size;
    uint32_t count;
};

//////////////////////////////////////////////////
// BD
// живёт на одном, своём потоке

// TODO change type
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

    // кастомизирую обработку сигналов
    signal(SIGINT, calling_stop);
    signal(SIGTERM, calling_stop);
    signal(SIGHUP, calling_reload);
    // игнорирую сигнал связанный с записью в закрытый другой стороной сокет
    // signal(SIGPIPE, SIG_IGN);
    

    SERVER_START:

    // TCP и UDP сокеты
    int tfd = 0, ufd = 0;
    {
    struct addrinfo hints;
    struct addrinfo *result;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int status = getaddrinfo(NULL, TPSTR, &hints, &result);
    if (status){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }
    status = socket_configure(&tfd, result);
    if (status){
	if (tfd)
		close(tfd);
        exit(EXIT_FAILURE);
    }
    }
    if (listen(tfd, SOMAXCONN) < 0){
        perror("listen");
	close(tfd);
	exit(EXIT_FAILURE);
    }
    {
    struct addrinfo hints;
    struct addrinfo *result;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    
    int status =  getaddrinfo(NULL, UPSTR, &hints, &result);
    if (status){
    	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
	close(tfd);
        exit(EXIT_FAILURE);
    }
    
    status = socket_configure(&ufd, result);
    if (status){
    	close(tfd);
	if (ufd)
		close(ufd);
	exit(EXIT_FAILURE);
    }
    }
    // epoll дескриптор - управляет обновлениями состояний, указанных с помощью epoll_ctl(... EPOLL_CTL_ADD ...) дескрипторов
    int epfd = epoll_create1(0);
    if (epfd == -1){
    	perror("epoll_create1");
	exit(EXIT_FAILURE);
    }

    // ev - временная переменная, events - хранят информацию о событиях
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN; // событие - ввод. EPOLLOUT - вывод, EPOLLRDHUP - отсоединение
    ev.data.fd = tfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &ev);
    
    ev.events = EPOLLIN;
    ev.data.fd = ufd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ufd, &ev);

    // Когда к нам подсоединяется новый клиент, он тоже добавляется к epfd. Кроме EPOLLIN он может в EPOLLRDHUP
    // Когда отсоединяется - удаляется.

    /* КОГДА epoll_wait обрабатывает EPOLLIN на tfd:
     * int client_fd = accept(tfd, ...)
     * set_nonblocking(client_fd);
     *
     * ev.events = EPOLLIN | EPOLLRDHUP
     * ev.data.fd = client_fd
     * epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev)
     *
     * КОГДА epoll_wait обрабатывает EPOLLIN на client_fd:
     * recv(fd, buffer, sizeof(buffer), 0);
     *
     * КОГДА epoll_wait обрабатывает EPOLLRDHUP:
     * 
     * КОГДА epoll_wait обрабатывает EPOLLIN:
     * recvfrom(ufd, buf, sizeof(buf), 0, (struct sockaddr*)&cliaddr, &len);
     *
     * */

    // Структура для хранения запросов к БД
    int status = Queue_construct(&task_queue, TASKS_STARTINGSIZE);
    if (status)
        exit(EXIT_FAILURE);


    pthread_t db_thread;
    // дескриптор, атрибуты (default), указатели на: функцию, параметры
    int thread_status = pthread_create(&db_thread, NULL, db_thread_fn, NULL);
    if (thread_status != 0){
        fprintf(stderr, "pthread_create, status=%d\n", thread_status);
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    // db_thread_fn работает

    while (!stop_server){
	int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
	for (int i = 0; i < n; ++i){
		int fd = events[i].data.fd;
		/*
		 * как-то поумнее
		 * */
		if (fd == tfd){
			// Думаем
		}
		else if (fd == ufd){
			// Пересылаем
		}
		else {
			// fd = client_fd
		}
	}
    }
    
    // завершение работы
    pthread_mutex_lock(&queue_mutex);
    stop_server = true;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);

    pthread_join(db_thread, NULL);

    if (reload_requested)
        goto SERVER_START;
    
    SERVER_END:
    Queue_destruct(&task_queue);
    close(tfd);
    close(ufd);

    return 0;
}
