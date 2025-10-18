#include "protocol.h"

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
