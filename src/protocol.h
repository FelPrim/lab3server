#pragma once

#define IS_SERVER

#ifdef __cpluscplus
extern "C" {
#endif



#define ERRORUNKNOWNSERVER 122
#define ERRORCALLJOINMEMBERSERVER 123
#define ERRORCALLNOTMEMBER 124
#define ERRORCALLNOPERMISSIONS 125
#define ERRORCALLNOTOWNER 126
#define ERRORCALLNOTEXISTSSERVER 127

#define ERRORUNKNOWNCLIENT 4

#define CALLSTARTCLIENT 0
#define CALLSTARTSERVER 128
#define CALLENDCLIENT 1
#define CALLENDSERVER 129
#define CALLJOINCLIENT 2
#define CALLJOINSERVER 130
#define CALLNEWMEMBERSERVER 131
#define CALLLEAVECLIENT 3
#define CALLLEAVESERVER 132
#define CALLLEAVEOWNERSERVER 133



#define SYMM_KEY_LEN 32

int encrypt(unsigned char *plaintext, int plaintext_len,
unsigned char *key, unsigned char *ciphertext);


int decrypt(unsigned char *ciphertext, int ciphertext_len, 
            unsigned char *key, unsigned char *plaintext);

#ifdef __cplusplus
}
#endif
