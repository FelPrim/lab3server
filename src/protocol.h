#pragma once
#include "useful_stuff.h"

#include <stdint.h>
#include <openssl/evp.h>

// Порт TCP для TLS соединений
#define TPORT 23230
#define UPORT 23231
#define TPSTR "23230"
#define UPSTR "23231"

// Максимальные размеры
#define MAXTLSSZ 2048
#define CALL_MAXSZ 64
#define CALL_NAME_SZ 7
#define SYMM_KEY_LEN 32

// Коды ошибок
#define ERRORUNKNOWNSERVER 122
#define ERRORCALLJOINMEMBER 123
#define ERRORCALLNOTMEMBER 124
#define ERRORCALLNOPERMISSIONS 125
#define ERRORCALLNOTOWNER 126
#define ERRORCALLNOTEXISTS 127
#define ERRORUNKNOWNCLIENT 5

// Команды клиента
#define CALLSTARTCLIENT 1
#define CALLENDCLIENT 2
#define CALLJOINCLIENT 3
#define CALLLEAVECLIENT 4
#define INFOCLIENT 255

// Команды сервера
#define CALLSTARTSERVER 128
#define CALLENDSERVER 129
#define CALLJOINSERVER 130
#define CALLNEWMEMBERSERVER 131
#define CALLLEAVESERVER 132
#define CALLLEAVEOWNERSERVER 133

// Макрос для создания упакованных структур с проверкой размера
#define MessageStruct(name, args) \
    _Pragma("pack(push, 1)") \
    struct name{args;}; \
    _Pragma("pack(pop)") 

// Структуры сообщений
MessageStruct(ErrorInfo, 
    char command;   
    char previous_command;   
    uint32_t size;    
    char data[2040]
);

MessageStruct(ClientCommand, 
    char command;
);

MessageStruct(CallStartInfo, 
    char command;
    char callname[CALL_NAME_SZ];
    unsigned char sym_key[SYMM_KEY_LEN];
    uint32_t creator_fd;
);

MessageStruct(CallIdInfo,
    char command;
    char callname[CALL_NAME_SZ];
);

MessageStruct(CallFullInfo,
    char command;
    char callname[CALL_NAME_SZ];
    unsigned char sym_key[SYMM_KEY_LEN];
    uint32_t joiner_fd;
    uint8_t participants_count;
    uint32_t participants[CALL_MAXSZ];
);

MessageStruct(CallMemberInfo,
    char command;
    char callname[CALL_NAME_SZ];
    uint32_t member_id;
);

MessageStruct(CallUpdateInfo,
    char command;
    char callname[CALL_NAME_SZ];
    uint32_t member_id;
    uint8_t participants_count;
);

MessageStruct(InfoClient,
    char command;
    uint16_t udp_port;
);

// Функции шифрования/дешифрования
int encrypt(unsigned char *plaintext, int plaintext_len,
            unsigned char *key, unsigned char *ciphertext);

int decrypt(unsigned char *ciphertext, int ciphertext_len,
            unsigned char *key, unsigned char *plaintext);

// Вспомогательные функции для работы с сетевым порядком байт
inline static uint32_t htonl_u32(uint32_t hostlong) {
    return htonl(hostlong);
}

inline static uint32_t ntohl_u32(uint32_t netlong) {
    return ntohl(netlong);
}

inline static uint16_t htons_u16(uint16_t hostshort) {
    return htons(hostshort);
}

inline static uint16_t ntohs_u16(uint16_t netshort) {
    return ntohs(netshort);
}

