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
    _Pragma("pack(push, 1)") \
    struct name{args;}; \
    _Pragma("pack(pop)") \
    static_assert(sizeof(struct name) <= MAXTLSSZ, "Struct "#name" too large")


MessageStruct(ErrorInfo, 
    char command;   
    char previous_command;   
    int size;    
    char data[2042]
);

#define ERRORCALLJOINMEMBER 123
#define ERRORCALLNOTMEMBER 124
#define ERRORCALLNOPERMISSIONS 125
#define ERRORCALLNOTOWNER 126
#define ERRORCALLNOTEXISTS 127

#define ERRORUNKNOWNCLIENT 5

#define CALLSTARTCLIENT 1
MessageStruct(ClientCommand, char command;);

#define CALLSTARTSERVER 128
MessageStruct(CallStartInfo, 
    char command;
    char callname[CALL_NAME_SZ];
    char sym_key[SYMM_KEY_LEN];
    int creator_fd;
);

#define CALLENDCLIENT 2
MessageStruct(CallIdInfo,
    char command;
    char callname[CALL_NAME_SZ];
);
#define CALLENDSERVER 129
#define CALLJOINCLIENT 3

#define CALLJOINSERVER 130
MessageStruct(CallFullInfo,
    char command;
    char callname[CALL_NAME_SZ];
    char sym_key[SYMM_KEY_LEN];
    int joiner_fd;
    char participants_count;
    int participants[255];
);

#define CALLNEWMEMBERSERVER 131
MessageStruct(CallMemberInfo,
    char command;
    char callname[CALL_NAME_SZ];
    int member_id;
);
#define CALLLEAVECLIENT 4
#define CALLLEAVESERVER 132
MessageStruct(CallUpdateInfo,
    char command;
    char callname[CALL_NAME_SZ];
    int member_id;
    char participants_count;
);
#define CALLLEAVEOWNERSERVER 133

#define INFOCLIENT 255
MessageStruct(InfoClient,
    char command;
    uint16_t udp_port;
);

#define SYMM_KEY_LEN 32

int encrypt(unsigned char *plaintext, int plaintext_len,
unsigned char *key, unsigned char *ciphertext);


int decrypt(unsigned char *ciphertext, int ciphertext_len, 
            unsigned char *key, unsigned char *plaintext);


#ifdef __cplusplus
}
#endif
