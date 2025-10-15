#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <time.h>

#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <signal.h>
#include <pthread.h>

// deps
#include <sqlite3.h>
#include <sodium.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "uthash.h"

#include "protocol.h"

#define OPSLIMIT_VERY_LOW 1
#define MEMLIMIT_VERY_LOW (1 << 12)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

//////////////////////////////////////////////////
// SIGNALS

volatile sig_atomic_t stop_requested = 0;
volatile sig_atomic_t reload_requested = 0;

void calling_stop(int signo){
    stop_requested = 1;
}

void calling_reload(int signo){
    reload_requested = 1;
    stop_requested = 1;
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
    for (p = result; p != NULL; p=p->ai_next){
        if ((*sock_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) < 0){
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

// TODO подумать над тем, чтобы хранить буфер в стеке, а не куче
struct Buffer{
    char *mem;
    uint32_t size;
    uint32_t seek;
    uint32_t expected_endseek;
    // bool reading_data = expected_end != 0
};

#define BASIC_SZ 1024

int Buffer_construct(struct Buffer* self){
    self->mem = malloc(BASIC_SZ);
    if (!self->mem){
        perror("malloc");
        return 1;
    }
    self->size = BASIC_SZ;
    self->seek = 0;
    self->expected_endseek = 0;
    return 0;
}

int Buffer_destruct(struct Buffer* self){
    free(self->mem);
    return 0;
}

int Buffer_clean(struct Buffer* self){
    self->seek = 0;
    self->expected_endseek = 0;
}

// -1 -> Ошибка
// 0 -> ждёт ещё
// 1 -> Полон
// 2 -> Переполнен
int Buffer_write(struct Buffer* self, char *data, uint32_t size){
    if (unlikely(self->seek+size > self->size)){
        assert(self->size < UINT_MAX/4);
        uint32_t new_size = self->size * 4;
        void* new_mem = realloc(self->mem, new_size);
        if (!new_mem){
            perror("realloc");
            new_size = self->seek + size;
            new_mem = realloc(self->mem, new_size);
            if (!new_mem){
                perror("smallest realloc possible");
                Buffer_clean(self);
                return -1;
            }
        }
        self->mem = new_mem;
    }
    memcpy(self->mem+self->seek, data, size);
    self->seek += size;
    if (self->seek == self->expected_endseek)
        return 1;
    if (self->seek > self->expected_endseek)
        return 2;
    return 0;
}


typedef struct Connection{
    struct Buffer buffer;
    struct sockaddr_in udp_addr; 
    SSL *ssl;
    int client_fd; // key
    int client_id; 
    UT_hash_handle hh;
} Connection;

Connection *connections = NULL;

int Connection_construct(Connection* self){
    self->ssl = NULL;
    return Buffer_construct(&self->buffer);
    // TODO обмозговать
}

int Connection_destruct(Connection* self){
    if (self->ssl)
        SSL_free(self->ssl);
    return Buffer_destruct(&self->buffer);
}

void Connection_add(Connection* elem){
	HASH_ADD_INT(connections, client_fd, elem);
}

Connection* Connection_find(int client_fd){
	Connection* result;
	HASH_FIND_INT(connections, &client_fd, result);
	return result;
}

void Connection_delete(Connection* connection){
	HASH_DEL(connections, connection);
	Connection_destruct(connection);
	// TODO стоит ли free(connection)?
}

// из-за того, как работает epoll, задача такая: минимизировать время нахождения соединения при известном сокете.
// => структура хеш-таблица. Загуглил - можно использовать uthash вместо того, чтоб писать самому
//struct Connections{
//    struct Connection *connections;
//    int *client_fds;
//    int *client_ids;
//    uint32_t *free_mem; // указывают на места, выделенные под хранение удалённых соединений
//    uint32_t size;
//    uint32_t free_size;
//    uint32_t count;
//    uint32_t free_count;
//};
/*
int Connections_construct(struct Connections* self, uint32_t size, uint32_t free_size){
    self->size = size;
    self->connections = malloc(self->size*sizeof(struct Connection));
    if (!self->connections){
        perror("malloc");
	goto CC_connections_handler;
    }
    uint32_t last_ok_i_plus_one = 0;
    for (uint32_t i = 0; i < self->size; ++i){
        if (!Connection_construct(self->connections+i)){
            perror("Connection_construct");
	    goto CC_connection_construct_handler;
        }
        else
            last_ok_i_plus_one = i+1;
    }
    self->client_fds = calloc(self->size, sizeof(int));
    if (!self->client_fds){
        perror("calloc");
	goto CC_fds_calloc_handler;
    }
    self->client_ids = calloc(self->size, sizeof(int));
    if (!self->client_ids){
        perror("calloc");
	goto CC_ids_calloc_handler;
    }
    self->free_size = free_size;
    self->free_mem = malloc(self->free_size*sizeof(uint32_t));
    if (!self->free_mem){
        perror("malloc");
	goto CC_free_mem_malloc_handler;
    }
    self->count = 0;
    self->free_count = 0;
    return 0;

CC_free_mem_malloc_handler:
    free(self->client_ids);
CC_ids_calloc_handler:
    free(self->client_fds);
CC_fds_calloc_handler:
CC_connection_construct_handler:
    for (uint32_t i = 0; i < last_ok_i_plus_one; ++i)
        Connection_destruct(self->connections+i);
    free(self->connections);
CC_connections_handler:
    return 1;
}

int Connections_destruct(struct Connections* self){
    int status = 0;
    for (uint32_t i = 0; i < self->size; ++i)
        status |= Connection_destruct(self->connections+i);
    free(self->connections);
    free(self->client_fds);
    free(self->client_ids);
    free(self->free_mem);
    return status;
}
*/

// тоже должно быстро определяться. В данном случае по callname.
typedef struct Call{
    int *participants; // первый участник - владелец конференции
    uint32_t size;
    uint32_t count;
    char callname[7]; // key
    UT_hash_handle hh;
} Call;

Call *calls = NULL;

int Call_construct(Call* self, uint32_t size){
    self->size = size;
    self->participants = calloc(size, sizeof(int));
    if (!self->participants){
    	perror("calloc");
	return 1;
    }
    self->count = 0;
    static_assert(RAND_MAX == 2147483647);
    int r = rand();
    for (int i = 0; i < 6; ++i){
    	self->callname[i] = r%26;
	r /= 26;
    }
    self->callname[6] = '\0';
    return 0;
}

int Call_destruct(Call* self){
    free(self->participants);
    return 0;
}

void Call_add(Call* elem){
	HASH_ADD_STR(calls, callname, elem);
}

Call* Call_find(char callname[7]){
	Call* result;
	HASH_FIND_STR(calls, &callname, result);
	return result;
}

void Call_delete(Call* call){
	HASH_DEL(calls, call);
	Call_destruct(call);
	// TODO подумать о том, стоит ли free(call)?
}

/*struct Calls{
    struct Call *calls; // TODO раскумекать
    uint32_t *free_mem;
    uint32_t size;
    uint32_t count;
    uint32_t free_size;
    uint32_t free_count;
};
*/
/*
struct User{
    char *login;
    int client_id;
    struct User friends[friends_count];
    struct User recievers[recievers_count];
    struct User senders[senders_count];
    int client_fd;
};
*/

//////////////////////////////////////////////////
// BD
// живёт на одном, своём потоке

// Добавлять / изменять / удалять - 2 бита
// id БД. Базы данных:
// Authentication, Friend Requests, Friends - так-то 2 бита
// Дополнительная информация
//            | Authentication     | Friend Requests           | Friends
// Удаление   | логин              | отправитель-получатель    | друг1-друг2
// Добавление | логин-пароль       | отправитель-получатель    | друг1-друг2
// Изменение  | логин-новый пароль | ???                       | ???
// Запрос     | логин->пароль      | получатель -> отправители | человек->все друзья
// Из-за Authentication хз какой размер дополнительной информации - поэтому дополнительная информация пусть будет указателем на реальную дополнительную информацию.
// Пусть логин будет не больше 256 байт. + байт на длину логина. Также в 1 бите хранится информация о том, используется только ASCII или всё-таки используется Unicode
// Аналогично с паролем

// TODO change type
//typedef int TASK; // не int

typedef struct Task{
    void* payload;
    unsigned int size;
    unsigned short id; // id относится к таблице
    unsigned char command; // всё равно sizeof(TASK) = 16
} TASK;



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
int Queue_destruct(struct Queue* self){
    assert(self);
    free(self->mem);
    return 0;
}

int Queue_append(struct Queue* self, TASK elem){
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
    ((TASK*) self->mem)[index] = elem;
    self->count++;
    return 0;
} 

int Queue_shift(struct Queue* self){
    assert(self);
    assert(self->count > 0);
    self->shift = (self->shift + 1) % self->size;
    self->count--;
    return 0;
}

TASK Queue_get(struct Queue* self){
    TASK elem = *((TASK*)(self->mem)+self->shift);
    Queue_shift(self);
    return elem;
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

// threadsafe queue
struct QueueTS{
    struct Queue queue;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
};

int QueueTS_construct(struct QueueTS* self, uint32_t size){
    int result = Queue_construct(&self->queue, size);
    if (unlikely(result))
        return result;
    result = pthread_mutex_init(&self->mutex, NULL);
    if (unlikely(result)){
        Queue_destruct(&self->queue);
        return result;
    }
    result = pthread_cond_init(&self->not_empty, NULL);
    if (unlikely(result)){
        pthread_mutex_destroy(&self->mutex);
        Queue_destruct(&self->queue);
        return result;
    }
    return result;
}

int QueueTS_destruct(struct QueueTS* self){
    int result = pthread_cond_destroy(&self->not_empty);
    if (unlikely(result)){
        perror("pthread_cond_destroy");
    }
    int result2 = pthread_mutex_destroy(&self->mutex);
    if (unlikely(result2)){
        perror("pthread_mutex_destroy");
    }
    result |= result2; // подумать о том, чтобы result = (result << x) | result2, x - неизвестная
    result2 = Queue_destruct(&self->queue);
    if (unlikely(result2)){
        perror("Queue_destruct");
    }
    return result | result2;
}

int QueueTS_append(struct QueueTS* self, TASK elem){
    pthread_mutex_lock(&self->mutex);
    int result = Queue_append(&self->queue, elem);
    pthread_cond_signal(&self->not_empty);
    pthread_mutex_unlock(&self->mutex);
    return result;
}

// возвращает 1 если stop_server и count = 0
int QueueTS_wait_and_get(struct QueueTS* self, TASK* result){
    pthread_mutex_lock(&self->mutex);
    while (self->queue.count == 0 && !stop_requested){
        pthread_cond_wait(&self->not_empty, &self->mutex);
    }
    if (self->queue.count == 0 && stop_requested){
        pthread_mutex_unlock(&self->mutex);
        return 1;
    }
    *result = Queue_get(self);
    pthread_mutex_unlock(&self->mutex);
    return 0;
}

// работает на одном потоке
struct Worker{
    struct QueueTS submission_queue;
    struct QueueTS completion_queue;

    sqlite3 *db;
};

typedef struct Result{
    void* payload; // size от 0 до 2^24-1
    int id; // id относится к клиенту
    unsigned int command_and_size;
} Result;

inline unsigned char Result_get_command(Result self){
    return self.command_and_size >> 24; // 2^(32-8)
}

inline unsigned int Result_get_size(Result self){
    return self.command_and_size & 0xffffff; // 16=2^4 => 2^24 = 2^(4*6)=16^6
}

int Queue_append_result(struct Queue* self, Result elem){
    assert(self);
    if (unlikely(self->count >= self->size)){
        fprintf(stderr, "%d\n", self->size);
        uint32_t right_part = self->size - self->shift;
        // 4567123
        // 0123456 (uintptr_t) first - (uintptr_t) mem   
        uint32_t new_sz = self->size*2;
        void* new_mem = malloc(new_sz*sizeof(Result));
        if (new_mem == NULL){
            perror("[malloc]");
            return EXIT_FAILURE;
        }
        //if (likely(shift != 0)){
        //    // 2345671 - особого случая нет
        //    memcpy(new_mem, self->first, sizeof(TASK)*(right_part));
        //    memcpy(new_mem+right_part, mem, sizeof(TASK)*shift);
        //}
        memcpy(new_mem, (Result*)self->mem + self->shift, sizeof(Result)*right_part);
        if (likely(self->shift != 0)){
            memcpy((Result*)new_mem+right_part, self->mem, sizeof(Result)*self->shift);
        }
        free(self->mem);
        self->mem = new_mem;
        self->shift = 0;
        self->size = new_sz;
    }
    uint32_t index = (self->shift+self->count)%self->size;
    ((Result*) self->mem)[index] = elem;
    self->count++;
    return 0;
} 

Result Queue_get_result(struct Queue* self){
    Result elem = *((Result*)(self->mem)+self->shift);
    Queue_shift(self);
    return elem;
}

int QueueTS_append_result(struct QueueTS* self, Result elem){
    pthread_mutex_lock(&self->mutex);
    int result = Queue_append_result(&self->queue, elem);
    pthread_cond_signal(&self->not_empty);
    pthread_mutex_unlock(&self->mutex);
    return result;
}

int QueueTS_wait_and_get_result(struct QueueTS* self, Result* result){
    pthread_mutex_lock(&self->mutex);
    while (self->queue.count == 0 && !stop_requested){
        pthread_cond_wait(&self->not_empty, &self->mutex);
    }
    if (self->queue.count == 0 && stop_requested){
        pthread_mutex_unlock(&self->mutex);
        return 1;
    }
    *result = Queue_get_result(self);
    pthread_mutex_unlock(&self->mutex);
    return 0;
}

#define TASKS_STARTINGSIZE 32

// epfd - дескриптор, задающий "центр асинхронности"
// event_fd - счётчик. Можно из одного потока увеличить значение счётчика, и тогда epoll заметит что произошел EPOLLIN
int Worker_construct(struct Worker* self, int *epfd, int *event_fd){
    int result = QueueTS_construct(&self->completion_queue, TASKS_STARTINGSIZE);
    if (unlikely(result)){
        perror("completion_queue construct");
        return result;
    }
    result = QueueTS_construct(&self->submission_queue, TASKS_STARTINGSIZE);
    if (unlikely(result)){
        perror("submission_queue construct");
        QueueTS_destruct(&self->completion_queue);
        return result;
    }
    result = sqlite3_open("data.db", &self->db);
    if (unlikely(result != SQLITE_OK)){
        perror("data.db open");
        QueueTS_destruct(&self->submission_queue);
        QueueTS_destruct(&self->completion_queue);
        return result;
    }
    const char CREATE_TABLES_IF_THEY_DONT_EXIST[] = 
    "PRAGMA foreign_keys = ON; "
    "CREATE TABLE IF NOT EXISTS authentication ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT."
    " login TEXT UNIQUE,"
    " hashed_password TEXT NOT NULL); "
    "CREATE TABLE IF NOT EXISTS friends ("
    " lfriend INTEGER,"
    " rfriend INTEGER,"
    " PRIMARY KEY (lfriend, rfriend),"
    " FOREIGN KEY (lfriend) REFERENCES authentication(id) ON DELETE RESTRICT,"
    " FOREIGN KEY (rfriend) REFERENCES authentication(id) ON DELETE RESTRICT,"
    " CHECK (lfriend != rfriend)"
    " );"
    "CREATE TABLE IF NOT EXISTS friend_requests ("
    " sender INTEGER,"
    " reciever INTEGER,"
    " PRIMARY KEY (sender, reciever),"
    " FOREIGN KEY (sender) REFERENCES authentication(id) ON DELETE RESTRICT,"
    " FOREIGN KEY (receiver) REFERENCES authentication(id) ON DELETE RESTRICT,"
    " CHECK (sender != receiver)"
    ");";
    char *err_msg = NULL;
    result = sqlite3_exec(self->db, CREATE_TABLES_IF_THEY_DONT_EXIST, NULL, NULL, &err_msg);
    if (unlikely(result != SQLITE_OK)){
        perror("data.db create table ine");
        fprintf(stderr, "%s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(self->db);
        QueueTS_destruct(&self->submission_queue);
        QueueTS_destruct(&self->completion_queue);
        return result;
    }

    *event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (*event_fd == -1) { 
        perror("eventfd"); 
        sqlite3_close(self->db);
        exit(1); 
    }

    struct epoll_event ev = { .events = EPOLLIN, .data = { .fd = event_fd } };
    if (epoll_ctl(*epfd, EPOLL_CTL_ADD, *event_fd, &ev) == -1) {
        perror("epoll_ctl add eventfd");
        sqlite3_close(self->db);
        exit(1);
    }

    return result;
}

int Worker_destruct(struct Worker* self){
    int result = QueueTS_destruct(&self->completion_queue);
    if (unlikely(result)){
        perror("QueueTS_destruct(completion_queue)");
    }
    int result2 = QueueTS_destruct(&self->submission_queue);
    if (unlikely(result2)){
        perror("QueueTS_destruct(submission_queue)");
    }
    result |= result2;
    result2 = sqlite3_close(self->db);
    if (unlikely(result2 != SQLITE_OK)){
        perror("sqlite3_close(db)");
    }
    return result | result2;
}

typedef struct Task{
    void* payload;
    unsigned int size;
    unsigned short id; // id относится к таблице
    unsigned char command; // всё равно sizeof(TASK) = 16
} TASK;

// Добавлять / изменять / удалять - 2 бита
// id БД. Базы данных:
// Authentication, Friend Requests, Friends - так-то 2 бита
// Дополнительная информация
//            | Authentication     | Friend Requests           | Friends
// Удаление   | логин              | отправитель-получатель    | друг1-друг2
// Добавление | логин-пароль       | отправитель-получатель    | друг1-друг2
// Изменение  | логин-новый пароль | ???                       | ???
// Запрос     | логин->пароль      | получатель -> отправители | человек->все друзья
// Из-за Authentication хз какой размер дополнительной информации - поэтому дополнительная информация пусть будет указателем на реальную дополнительную информацию.
// Пусть логин будет не больше 256 байт. + байт на длину логина. Также в 1 бите хранится информация о том, используется только ASCII или всё-таки используется Unicode
// Аналогично с паролем
typedef struct Result{
    void* payload; // size от 0 до 2^24-1
    int id; // id относится к клиенту
    unsigned int command_and_size;
} Result;

int post_login(const char* login, const char* password, sqlite3* db){
    char hash[crypto_pwhash_STRBYTES];
    int status = crypto_pwhash_str(
        hash, password, strlen(password),
        OPSLIMIT_VERY_LOW, MEMLIMIT_VERY_LOW
    );
    if (status){
        fprintf(stderr, "crypto_pwhash_str failed (insufficient memory?)\n");
        return -1;
    }

    const char *sql = "INSERT INTO authentication (login, hashed_pswd) VALUES (?, ?);";
    sqlite3_stmt *stmt = NULL;
    status = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare_v2: %s\n", sqlite3_errmsg(db));
        return status;
    }

    status = sqlite3_bind_text(stmt, 1, login, -1, SQLITE_TRANSIENT);
    if (status != SQLITE_OK) {
        fprintf(stderr, "sqlite3_bind_text (nick): %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return status;
    }

    status = sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);
    if (status != SQLITE_OK) {
        fprintf(stderr, "sqlite3_bind_text (hash): %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return status;
    }

    status = sqlite3_step(stmt);
    if (status != SQLITE_DONE) {
        fprintf(stderr, "sqlite3_step failed: %s (status=%d)\n", sqlite3_errmsg(db), status);
        sqlite3_finalize(stmt);
        int ext = sqlite3_extended_errcode(db);
        if (ext == SQLITE_CONSTRAINT_UNIQUE){
            // послать на клиент информацию о том, что такой логин уже зарегистрирован
            // для этого нужно узнать сокет клиента 
            
            // TODO
            //Result result = {.command_and_size, .id, .payload};
        }

        return status;
    }

    sqlite3_finalize(stmt);


    char template[]="INSERT INTO authentication VALUES (%s, %s);";
    char command[1024];
    // snprintf(command, sizeof(command), template, lgn, hash); TODO сделать адекватным
    char *err_msg = NULL;
    status = sqlite3_exec(db, command, NULL, NULL, err_msg);
    if (status){
        fprintf(stderr, "post_login: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    return status;
}

int delete_login(const char* login, sqlite3 *db){
    const char sql[] = "DELETE FROM authentication WHERE login = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK){
        fprintf(stderr, "sqlite3_prepare_v2: %s\n", sqlite3_errmsg(db));
        return rc;
    }
    rc = sqlite3_bind_text(stmt, 1, login, -1, SQLITE_STATIC);
    if (rc != SQLITE_OK){
        fprintf(stderr, "sqlite3_bind_text: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc;
    }
    rc = sqlite3_step(stmt);
    if  (rc != SQLITE_DONE){
        fprintf(stderr, "sqlite3_step: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc;
    }
    // TODO проверить что не бред
    int changes = sqlite3_changes(db);
    if (changes == 0){
        // пользователь не найден
    }
    else{
        // kjhkksfk
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;

    // TODO
}

int get_hashed_password(const char* login, sqlite3 *bd, char* *hashed_password){
    // TODO
}

int post_friend_request(int sender, int reciever, sqlite3 *bd){

}

int delete_friend_request(int sender, int reciever, sqlite3 *bd){}

int post_friendship(int friend1, int friend2, sqlite3 *bd){}
int delete_friendship(int friend1, int friend2, sqlite3 *bd){}
/*
void do_task(TASK task, struct Queue *task_queue, struct Queue *result_queue){
    switch (((int) (task.id) << 8)+task.command){
//////////////////// логин пароль
        case 0:
        { // добавление
        (struct Login_Password*)data = task.payload;
        
        break;
        }
        case 1:
        { // удаление
        (struct Login_Password*)data = task.payload;
        
        break;
        }
        //case 2:
        //{ // 
        //
        //break;
        //}
//////////////////// запрос на добавление в друзья
        case 0x100:{
        // добавление
        (struct User_User*)data = task.payload;

        break;
        }
        case 0x101:{
        // удаление
        (struct User_User*)data = task.payload;

        break;
        }
//////////////////// друзья
        case 0x200:{
        // добавление
        (struct User_User*)data = task.payload;

        break;
        }
        case 0x201:{
        // удаление
        (struct User_User*)data = task.payload;

        break;
        }
        default:


    }
}
*/
void* Worker_mainloop(void* args){
    assert(args);
    struct Worker* self = (struct Worker *)args;
    TASK task;
    while(1){
        int status = QueueTS_wait_and_get(&self->completion_queue, &task);
        if (status == 1){
            // stop_server = true, count = 0
            break;
        }
        do_task(task);
    }
    return NULL;
}


struct Queue task_queue;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  queue_cond  = PTHREAD_COND_INITIALIZER;



void* db_thread_fn(void* args){
    assert(task_queue.mem);
    while(1){
        pthread_mutex_lock(&queue_mutex);
        while (task_queue.count == 0 && !stop_requested){
            pthread_cond_wait(&queue_cond, &queue_mutex);
            // Если мы зашли в эту функцию, то выйдем только после того как где-то будет выполнен pthread_cond_signal(&queue_cond);
        }
        if (task_queue.count == 0 && stop_requested){
            // выход
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        // task_queue.count > 0
        TASK task = ((TASK*) task_queue.mem)[task_queue.shift]; // TODO проверить имеет ли смысл
        Queue_shift(&task_queue);
        pthread_mutex_unlock(&queue_mutex);
        
        do_task(task);
    }
}

int enqueue_task(TASK task){
    pthread_mutex_lock(&queue_mutex);
    int status = Queue_append(&task_queue, task);
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
    return status;
}

//////////////////////////////////////////////////
// LOGIC

int main(){
    srand((unsigned int)time(NULL));
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
    

    if (sodium_init() < 0) {
        perror("sodium");
        exit(EXIT_FAILURE);
    }



    SERVER_START:
    stop_requested = false;
    reload_requested = false;
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
    // TODO понять что такое status
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

    while (!stop_requested){
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
    stop_requested = true;
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
