#pragma once

#ifdef __cpluscplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
#endif


#define MAXBUFFER 65536
#define ADEQUATEBUFFER 8192


#define REGISTRATION 8
#define REGISTRATIONERROR 9
#define AUTHORIZATION 10
#define AUTHORIZATIONERROR 11
#define USERDELETION 12
#define USERDELETIONERROR 13
#define PASSWORDCHANGE 14
#define PASSWORDCHANGEERROR 15

#define FRIENDREQUEST 32
#define FRIENDREQUESTERROR 33
#define FRIENDREQUESTDELETION 34
#define FRIENDREQUESTDELETIONERROR 35
#define FRIENDREQUESTSTO 36
#define FRIENDREQUESTSTOSEND 37
#define FRIENDREQUESTSTOSENDERROR 38
#define FRIENDREQUESTSFROM 36
#define FRIENDREQUESTSFROMSEND 37
#define FRIENDREQUESTSFROMSENDERROR 38

#define FRIEND 48
#define FRIENDERROR 49
#define FRIENDDELETION 50
#define FRIENDDELETIONERROR 51
#define FRIENDSINFO 52
#define FRIENDSINFOSEND 53
#define FRIENDSINFOSENDERROR 54

// начать конфу
#define CALLSTART 64


#define CALLSTARTERROR 65
#define CALLSTARTWITH 66

// Ответ сервера на начало конфы
#define CALLSTARTSEND 67

// завершить конфу
#define CALLEND 68


#define CALLENDERROR 69

// конфу завершнили
#define CALLENDSERV 71



// присоединиться к конфе
#define CALLJOIN 72


#define CALLJOINERROR 73

// Ответ сервера на присоединение к конфе
#define CALLJOINSEND 75


#define CALLLEAVE 76
#define CALLLEAVEERROR 77
#define CALLINFO 78

// отправить текущую информацию о состоянии конфы. В нашем случае: к конфе кто-то присоединился
#define CALLINFOSEND 79


#define CALLINFOSENDERROR 80

/*
CALLSTART
CALLSTARTSEND
CALLEND
CALLENDSERV
CALLJOIN
CALLJOINSEND
CALLINFOSEND
*/


#pragma pack(push, 1)
struct RegistrationInfo{
    char id;
    uint32_t login_size;
    uint32_t password_size;
};
#pragma pack(pop)

char* format_for_sending_2str(const char *login, const char *password, char id){
    uint32_t login_sz = strlen(login)+1;
    uint32_t pswd_sz = strlen(password)+1;

    struct RegistrationInfo info = {.id = id, .login_size=htonl(login_sz), .password_size=htonl(pswd_sz)};
    uint32_t the_size = sizeof(info) + login_sz + pswd_sz;
    char* buffer = (char*) malloc(the_size);
    if (!buffer){
        perror("malloc");
        return NULL;
    }
    memcpy(buffer, &info, sizeof(info));
    memcpy(buffer + sizeof(info), login, login_sz);
    memcpy(buffer + sizeof(info) + login_sz, password, pswd_sz);

    return buffer;
}

char* format_for_sending_registration(const char *login, const char *password){
    return format_for_sending_2str(login, password, REGISTRATION);
}

#pragma pack(push, 1)
struct ErrorInfo{
    char id;
    uint32_t error_size;
};
#pragma pack(pop)

char* format_for_sending_error(const char *error, char id){
    uint32_t err_sz = strlen(error)+1;

    struct ErrorInfo info = {.id = id, .error_size=htonl(err_sz)};
    uint32_t the_size = sizeof(info) + err_sz;
    char *buffer = (char*) malloc(the_size);
    if (!buffer){
        perror("malloc");
        return NULL;
    }
    memcpy(buffer, &info, sizeof(info));
    memcpy(buffer + sizeof(info), error, err_sz);
    return buffer;
}
char* format_for_sending_registrationerror(const char *error){
    return format_for_sending_error(error, REGISTRATIONERROR);
}

char* format_for_sending_authorization(const char *login, const char *password){
    return format_for_sending_2str(login, password, AUTHORIZATION);
}
char* format_for_sending_authorizationerror(const char *error){
    return format_for_sending_error(error, AUTHORIZATIONERROR);
}

#pragma pack(push, 1)
struct UserDeletionInfo{
    char id;
};
#pragma pack(pop)

char* format_for_sending_noinfo(char id){
    //char buffer[1];
    //memcpy(buffer, &id, 1);
    return &id;
}

char* format_for_sending_userdeletion(){
    return format_for_sending_noinfo(USERDELETION);
}
char* format_for_sending_userdeletionerror(const char *error){
    return format_for_sending_error(error, USERDELETIONERROR);
}

char* format_for_sending_passwordchange(const char *password){
    return format_for_sending_error(password, PASSWORDCHANGE);
}

char* format_for_sending_passwordchangeerror(const char *error){
    return format_for_sending_error(error, PASSWORDCHANGE);
}



char* format_for_sending_friendrequest(const char *reciever){
    return format_for_sending_error(reciever, FRIENDREQUEST);
}
char* format_for_sending_friendrequesterror(const char *error){
    return format_for_sending_error(error, FRIENDREQUESTERROR);
}
char* format_for_sending_friendrequestdeletion( const char *sender){
    return format_for_sending_error(sender, FRIENDREQUESTDELETION);
}
char* format_for_sending_friendrequestdeletionerror(const char *error){
    return format_for_sending_error(error, FRIENDREQUESTDELETIONERROR);
}
char* format_for_sending_friendrequestto(){
    return format_for_sending_noinfo(FRIENDREQUESTSTO);
}

char* format_for_sending_strlist(const char** strlist, uint32_t count, char id){
    uint32_t szs[count]; // это C
    uint32_t the_size = 0;
    for (uint32_t i = 0; i < count; ++i){
        uint32_t sz = strlen(strlist[i])+1;
        the_size += sz;
        szs[i] = sz;
    }
    char* buffer = (char*) malloc(the_size+1); // id
    if (!buffer){
        perror("malloc");
        return NULL;
    }
    buffer[0] = id;
    memcpy(buffer+1, &the_size, sizeof(the_size));
    uint32_t shift = 1+sizeof(the_size);
    for (uint32_t i = 0; i < count; ++i){
        memcpy(buffer+shift, strlist[i], szs[i]);
        shift += szs[i];
    }
    return buffer;
}

char* format_for_sending_friendrequesttosend(const char** possiblefriends, uint32_t count){
    return format_for_sending_strlist(possiblefriends, count, FRIENDREQUESTSTOSEND);
}
char* format_for_sending_friendrequesttosenderror(const char* error){
    return format_for_sending_error(error, FRIENDREQUESTSTOSENDERROR);
}
char* format_for_sending_friendrequestfrom(){
    return format_for_sending_noinfo(FRIENDREQUESTSFROM);
}
char* format_for_sending_friendrequestfromsend(const char** possiblefriends, uint32_t count){
    return format_for_sending_strlist(possiblefriends, count, FRIENDREQUESTSFROMSEND);
}

char* format_for_sending_friendrequestfromsenderror(const char* error){
    return format_for_sending_error(error, FRIENDREQUESTSFROMSENDERROR);
}

char* format_for_sending_friendship(const char* Friend){
    return format_for_sending_error(Friend, FRIEND);
}
char* format_for_sending_friendshiperror(const char* error){
    return format_for_sending_error(error, FRIENDERROR);
}
char* format_for_sending_frienddeletion(const char* exfriend){
    return format_for_sending_error(exfriend, FRIENDDELETION);
}
char* format_for_sending_frienddeletionerror(const char* error){
    return format_for_sending_error(error, FRIENDDELETIONERROR);
}
char* format_for_sending_friendinfo(){
    return format_for_sending_noinfo(FRIENDSINFO);
}
char* format_for_sending_friendinfosend(const char** friends, uint32_t count){
    return format_for_sending_strlist(friends, count, FRIENDSINFOSEND);
}
char* format_for_sending_friendinosenderror(const char* error){
    return format_for_sending_noinfo(FRIENDSINFOSENDERROR);
}

char* format_for_sending_callstart(){
    return format_for_sending_noinfo(CALLSTART);
}
char* format_for_sending_callstarterror(const char *error){
    return format_for_sending_error(error, CALLSTARTERROR);
}
char* format_for_sending_callstartwith(const char* Friend){
    return format_for_sending_error(Friend, CALLSTARTWITH);
}
// 1 в списке участников - владелец конференции!!!
char* _format_for_sending_callinfo(const char* callname, const char** participants, uint32_t count, char id){
    uint32_t szs[count+1]; 
    szs[0] = strlen(callname)+1;
    uint32_t the_size = szs[0];
    for (uint32_t i = 0; i < count; ++i){
        uint32_t sz = strlen(participants[i])+1;
        the_size += sz;
        szs[i+1] = sz;
    }
    char* buffer = (char*) malloc(the_size+1); // id
    if (!buffer){
        perror("malloc");
        return NULL;
    }
    buffer[0] = id;
    memcpy(buffer+1, &the_size, sizeof(the_size));
    uint32_t shift = 1+sizeof(the_size);
    memcpy(buffer+shift, callname, szs[0]);
    shift += szs[0];
    for (uint32_t i = 0; i < count; ++i){
        memcpy(buffer+shift, participants[i], szs[i+1]);
        shift += szs[i+1];
    }
    return buffer;
}

char* format_for_sending_callstartsend(const char* callname, const char** participants, uint32_t count){
    return _format_for_sending_callinfo(callname, participants, count, CALLSTARTSEND);
}

char* format_for_sending_callend(const char* callname){
    return format_for_sending_error(callname, CALLEND);
}

char* format_for_sending_calljoin(const char* call){
    return format_for_sending_error(call, CALLJOIN);
}

char* format_for_sending_calljoinerror(const char* error){
    return format_for_sending_error(error, CALLJOINERROR);
}

char* format_for_sending_calljoinsend(const char* call, const char** participants, uint32_t count){
    return _format_for_sending_callinfo(call, participants, count, CALLJOINSEND);
}
char* format_for_sending_callleave(const char* call){
    return format_for_sending_error(call, CALLLEAVE);
}
char* format_for_sending_callleaveerror(const char *error){
    return format_for_sending_error(error, CALLLEAVEERROR);
}
char* format_for_sending_callinfo(const char* call){
    return format_for_sending_error(call, CALLINFO);
}
char* format_for_sending_callinfosend(const char* call, const char** participants, uint32_t count){
    return _format_for_sending_callinfo(call, participants, count, CALLINFOSEND);
}
char* format_for_sending_callinfosenderror(const char* error){
    return format_for_sending_error(error, CALLINFOSENDERROR);
}

#ifdef __cplusplus
}
#endif