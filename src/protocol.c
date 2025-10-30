#include "protocol.h"
#include <openssl/evp.h>
#include <openssl/err.h>
#include <string.h>

int encrypt(unsigned char *plaintext, int plaintext_len,
            unsigned char *key, unsigned char *ciphertext) {
    EVP_CIPHER_CTX *ctx;
    int len;
    int ciphertext_len;

    // Создаем и инициализируем контекст
    if (!(ctx = EVP_CIPHER_CTX_new())) {
        return -1;
    }

    // Инициализируем операцию шифрования
    // ВАЖНО: ECB режим не рекомендуется для реальных приложений,
    // но используется здесь для простоты учебного проекта
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, key, NULL)) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    // Отключаем padding для фиксированного размера данных
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    // Шифруем данные
    if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len)) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len = len;

    // Завершаем шифрование
    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len += len;

    // Очищаем контекст
    EVP_CIPHER_CTX_free(ctx);

    return ciphertext_len;
}

int decrypt(unsigned char *ciphertext, int ciphertext_len,
            unsigned char *key, unsigned char *plaintext) {
    EVP_CIPHER_CTX *ctx;
    int len;
    int plaintext_len;

    // Создаем и инициализируем контекст
    if (!(ctx = EVP_CIPHER_CTX_new())) {
        return -1;
    }

    // Инициализируем операцию дешифрования
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, key, NULL)) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    // Отключаем padding
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    // Дешифруем данные
    if (1 != EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len)) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len = len;

    // Завершаем дешифрование
    if (1 != EVP_DecryptFinal_ex(ctx, plaintext + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    plaintext_len += len;

    // Очищаем контекст
    EVP_CIPHER_CTX_free(ctx);

    return plaintext_len;
}

// Вспомогательная функция для безопасного копирования имен конференций
void safe_callname_copy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    
    size_t copy_len = dest_size - 1; // Оставляем место для нуль-терминатора
    if (strlen(src) < copy_len) {
        copy_len = strlen(src);
    }
    
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

// Функция для проверки корректности имени конференции
int is_valid_callname(const char *callname) {
    if (!callname) return 0;
    
    size_t len = strlen(callname);
    if (len == 0 || len >= CALL_NAME_SZ) return 0;
    
    // Проверяем, что имя состоит только из букв A-Z
    for (size_t i = 0; i < len; i++) {
        if (callname[i] < 'A' || callname[i] > 'Z') {
            return 0;
        }
    }
    
    return 1;
}

// Функция для создания структуры ErrorInfo
void create_error_info(struct ErrorInfo *error_info, char command, char prev_command, 
                       const char *data, uint32_t data_size) {
    if (!error_info) return;
    
    error_info->command = command;
    error_info->previous_command = prev_command;
    error_info->size = htonl_u32(data_size);
    
    if (data && data_size > 0 && data_size <= sizeof(error_info->data)) {
        memcpy(error_info->data, data, data_size);
    }
}

// Функция для чтения структуры ErrorInfo
void read_error_info(const struct ErrorInfo *error_info, char *command, char *prev_command,
                     char *data, uint32_t *data_size) {
    if (!error_info) return;
    
    if (command) *command = error_info->command;
    if (prev_command) *prev_command = error_info->previous_command;
    
    uint32_t size = ntohl_u32(error_info->size);
    if (data_size) *data_size = size;
    
    if (data && size > 0 && size <= sizeof(error_info->data)) {
        memcpy(data, error_info->data, size);
    }
}
