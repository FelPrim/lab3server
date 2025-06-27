#pragma once

#define IS_SERVER

#ifdef __cpluscplus
extern "C" {
#endif


#define TPORT 23230
#define UPORT 23231
#define TPSTR "23230"
#define UPSTR "23231"

// все сообщения по TLS <= 2048 байт!
#define MAXTLSSZ 2048
#define CALL_MAXSZ 64
#define CALL_NAME_SZ 7
#define SYMM_KEY_LEN 32

#define ERRORUNKNOWNSERVER 122

#define MessageStruct(name, args) \
    #pragma pack(push, 1) \
    struct name{args}; \
    #pragma pack(pop) \
    static_assert(sizeof(struct name) <= MAXTLSSZ);

#pragma pack(push, 1)
struct ErrorInfo{
    char command;
    char previous_command;
    int size;
    char data[2042];
};
#pragma pack(pop)

static_assert(sizeof(struct ErrorInfo) <= MAXTLSSZ);

#define ERRORCALLJOINMEMBER 123
#define ERRORCALLNOTMEMBER 124
#define ERRORCALLNOPERMISSIONS 125
#define ERRORCALLNOTOWNER 126
#define ERRORCALLNOTEXISTS 127

#define ERRORUNKNOWNCLIENT 5

#define CALLSTARTCLIENT 1
#pragma pack(push, 1)
struct ClientCommand{
    char command;
};
#pragma pack(pop)
static_assert(sizeof(struct ClientCommand) <= MAXTLSSZ);

#define CALLSTARTSERVER 128
struct CallStartInfo{
    char command;
    char callname[CALL_NAME_SZ];
    char sym_key[SYMM_KEY_LEN];
    int creator_fd;
};
#define CALLENDCLIENT 2
struct CallCommand{
    char command;
    char callname[CALL_NAME_SZ];
};
#define CALLENDSERVER 129
struct CallEndInfo{
    char command;
    char callname[CALL_NAME_SZ];
};
#define CALLJOINCLIENT 3

#define CALLJOINSERVER 130
struct CallJoinInfo{
    char command;
    char callname[CALL_NAME_SZ];
    char sym_key[SYMM_KEY_LEN];
    char participants_count;
    int participants[255];
};
static_assert(sizeof(struct CallJoinInfo) <= MAXTLSSZ);

#define CALLNEWMEMBERSERVER 131
#define CALLLEAVECLIENT 4
#define CALLLEAVESERVER 132
#define CALLLEAVEOWNERSERVER 133

#define INFOCLIENT 255


#define SYMM_KEY_LEN 32

<<<<<<< HEAD
int encrypt(unsigned char *plaintext, int plaintext_len,
unsigned char *key, unsigned char *ciphertext);


int decrypt(unsigned char *ciphertext, int ciphertext_len, 
            unsigned char *key, unsigned char *plaintext);
=======
struct Call{
    int participants[CALL_MAXSZ]; // первый участник - владелец конференции
    uint32_t count;
    char callname[CALL_NAME_SZ]; // key
    unsigned char symm_key[SYMM_KEY_LEN]; 
};

>>>>>>> 5eb0ff6 (changes)

#ifdef __cplusplus
}
#endif
