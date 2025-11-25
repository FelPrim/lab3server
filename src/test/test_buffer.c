#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../buffer.h"
#include "../buffer_logic.h"
#include "../protocol.h"
#include "../test_common.h"

// Объявление функции, которая определена в buffer_logic.c но не в header'е
void buffer_logic_set_expected_size_resolver(int (*resolver)(uint8_t, uint32_t*));
// Объявление функции, которая определена в buffer_logic.c но не в header'е
void buffer_logic_set_expected_size_resolver(int (*resolver)(uint8_t, uint32_t*));

bool test_buffer_init() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_init");
    
    Buffer b;
    buffer_init(&b);
    TEST_ASSERT(&ctx, b.position == 0, "Position should be 0 after init");
    TEST_ASSERT(&ctx, b.expected_size == 0, "Expected size should be 0 after init");
    
    TEST_REPORT(&ctx, "test_buffer_init");
}

bool test_buffer_reserve() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_reserve");
    
    Buffer b;
    buffer_init(&b);

    TEST_ASSERT(&ctx, buffer_reserve(&b, 10) == 0, "Should reserve 10 bytes");
    TEST_ASSERT(&ctx, b.position == 0, "Position should remain 0 after reserve");
    TEST_ASSERT(&ctx, b.expected_size == 10, "Expected size should be 10");

    // Неверный вызов - слишком большой размер
    TEST_ASSERT(&ctx, buffer_reserve(&b, BUFFER_SIZE + 1) != 0, "Should fail for too large size");

    TEST_REPORT(&ctx, "test_buffer_reserve");
}

bool test_buffer_write_basic() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_write_basic");
    
    Buffer b;
    buffer_init(&b);

    buffer_reserve(&b, 8);

    uint8_t d1[5] = {1,2,3,4,5};
    TEST_ASSERT(&ctx, buffer_write(&b, d1, 5) == BUFFER_IS_INCOMPLETE, "Should be incomplete after 5/8 bytes");
    TEST_ASSERT(&ctx, b.position == 5, "Position should be 5");
    TEST_ASSERT(&ctx, b.expected_size == 8, "Expected size should remain 8");

    uint8_t d2[3] = {6,7,8};
    TEST_ASSERT(&ctx, buffer_write(&b, d2, 3) == BUFFER_IS_COMPLETE, "Should be complete after 8/8 bytes");
    TEST_ASSERT(&ctx, b.position == 8, "Position should be 8");
    // expected_size НЕ должен сбрасываться в 0 после полной записи
    TEST_ASSERT(&ctx, b.expected_size == 8, "Expected size should remain 8 after complete write");

    TEST_REPORT(&ctx, "test_buffer_write_basic");
}

bool test_buffer_write_overflow() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_write_overflow");
    
    Buffer b;
    buffer_init(&b);

    buffer_reserve(&b, 5);

    uint8_t d1[6] = {1,2,3,4,5,6};
    
    // Слишком много данных - должно вернуть OVERFLOW
    TEST_ASSERT(&ctx, buffer_write(&b, d1, 6) == BUFFER_OVERFLOW, "Should return overflow for too much data");
    TEST_ASSERT(&ctx, b.position == 0, "Position should not change on overflow");
    TEST_ASSERT(&ctx, b.expected_size == 5, "Expected size should remain 5");

    TEST_REPORT(&ctx, "test_buffer_write_overflow");
}

bool test_buffer_read_basic() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_read_basic");
    
    Buffer b;
    buffer_init(&b);

    // Записываем данные без использования expected_size для простоты
    uint8_t src[8] = {10,20,30,40,50,60,70,80};
    TEST_ASSERT(&ctx, buffer_write(&b, src, 8) == BUFFER_IS_INCOMPLETE, "Should be incomplete without expected size");
    TEST_ASSERT(&ctx, b.position == 8, "Position should be 8");

    // Читаем 5 байт
    uint8_t out1[5];
    // Без expected_size всегда возвращает INCOMPLETE
    TEST_ASSERT(&ctx, buffer_read(&b, out1, 5) == BUFFER_IS_INCOMPLETE, "Should be incomplete after partial read");
    TEST_ASSERT(&ctx, b.position == 3, "Position should be 3 after reading 5 from 8");
    TEST_ASSERT(&ctx, memcmp(out1, (uint8_t[]){10,20,30,40,50}, 5) == 0, "First 5 bytes should match");

    // Читаем оставшиеся 3 байта
    uint8_t out2[3];
    // Без expected_size всегда возвращает INCOMPLETE
    TEST_ASSERT(&ctx, buffer_read(&b, out2, 3) == BUFFER_IS_INCOMPLETE, "Should be incomplete after full read without expected size");
    TEST_ASSERT(&ctx, b.position == 0, "Position should be 0 after reading all");
    TEST_ASSERT(&ctx, memcmp(out2, (uint8_t[]){60,70,80}, 3) == 0, "Last 3 bytes should match");

    TEST_REPORT(&ctx, "test_buffer_read_basic");
}

bool test_buffer_read_with_expected() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_read_with_expected");
    
    Buffer b;
    buffer_init(&b);

    // Устанавливаем expected_size и записываем данные
    buffer_reserve(&b, 8);
    uint8_t src[8] = {10,20,30,40,50,60,70,80};
    TEST_ASSERT(&ctx, buffer_write(&b, src, 8) == BUFFER_IS_COMPLETE, "Should be complete after writing expected amount");
    TEST_ASSERT(&ctx, b.position == 8, "Position should be 8");
    TEST_ASSERT(&ctx, b.expected_size == 8, "Expected size should be 8");

    // Читаем 5 байт
    uint8_t out1[5];
    TEST_ASSERT(&ctx, buffer_read(&b, out1, 5) == BUFFER_IS_COMPLETE, "Should be complete after partial read (position == expected_size)");
    TEST_ASSERT(&ctx, b.position == 3, "Position should be 3 after reading 5 from 8");
    TEST_ASSERT(&ctx, b.expected_size == 3, "Expected size should be 3 after reading 5 from 8");
    TEST_ASSERT(&ctx, memcmp(out1, (uint8_t[]){10,20,30,40,50}, 5) == 0, "First 5 bytes should match");

    // Читаем оставшиеся 3 байта
    uint8_t out2[3];
    TEST_ASSERT(&ctx, buffer_read(&b, out2, 3) == BUFFER_IS_INCOMPLETE, "Should be incomplete after reading all (expected_size becomes 0)");
    TEST_ASSERT(&ctx, b.position == 0, "Position should be 0");
    TEST_ASSERT(&ctx, b.expected_size == 0, "Expected size should be 0 after reading all");
    TEST_ASSERT(&ctx, memcmp(out2, (uint8_t[]){60,70,80}, 3) == 0, "Last 3 bytes should match");

    TEST_REPORT(&ctx, "test_buffer_read_with_expected");
}

bool test_buffer_read_overflow() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_read_overflow");
    
    Buffer b;
    buffer_init(&b);

    // Записываем 5 байт
    uint8_t data[5] = {1,2,3,4,5};
    TEST_ASSERT(&ctx, buffer_write(&b, data, 5) == BUFFER_IS_INCOMPLETE, "Should be incomplete after writing 5 bytes");
    TEST_ASSERT(&ctx, b.position == 5, "Position should be 5");

    uint8_t out[10];
    memset(out, 0xAA, sizeof(out));

    // Пытаемся прочитать больше чем есть
    TEST_ASSERT(&ctx, buffer_read(&b, out, 10) == BUFFER_OVERFLOW, "Should return overflow when reading more than available");
    TEST_ASSERT(&ctx, b.position == 5, "Position should not change on read overflow");

    // Проверяем что буфер назначения не изменился
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(&ctx, out[i] == 0xAA, "Output buffer should remain unchanged on overflow");
    }

    TEST_REPORT(&ctx, "test_buffer_read_overflow");
}

bool test_buffer_state() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_state");
    
    Buffer b;
    buffer_init(&b);

    // Тестируем без expected_size
    TEST_ASSERT(&ctx, buffer_state(&b) == BUFFER_IS_INCOMPLETE, "Should be incomplete with no data");

    // Добавляем данные
    uint8_t data[3] = {1,2,3};
    buffer_write(&b, data, 3);
    TEST_ASSERT(&ctx, buffer_state(&b) == BUFFER_IS_INCOMPLETE, "Should be incomplete without expected size");

    // Тестируем с expected_size
    buffer_clear(&b);
    buffer_reserve(&b, 4);
    TEST_ASSERT(&ctx, buffer_state(&b) == BUFFER_IS_INCOMPLETE, "Should be incomplete with expected size but no data");

    buffer_write(&b, data, 2);
    TEST_ASSERT(&ctx, buffer_state(&b) == BUFFER_IS_INCOMPLETE, "Should be incomplete with partial data");

    buffer_write(&b, data, 2);
    TEST_ASSERT(&ctx, buffer_state(&b) == BUFFER_IS_COMPLETE, "Should be complete with all expected data");

    TEST_REPORT(&ctx, "test_buffer_state");
}

// Mock функция для тестов
static int mock_expected_size(uint8_t type, uint32_t* out) {
    // Для тестов используем простую логику: размер = тип * 2
    // Но для известных типов возвращаем правильные размеры
    switch (type) {
        case CLIENT_STREAM_CREATE:
            *out = 1 + sizeof(StreamCreatePayload);
            return 0;
        case CLIENT_STREAM_DELETE:
        case CLIENT_STREAM_CONN_JOIN:
        case CLIENT_STREAM_CONN_LEAVE:
            *out = 1 + sizeof(StreamIDPayload);
            return 0;
        case CLIENT_CALL_CREATE:
            *out = 1;  // Только тип
            return 0;
        default:
            *out = type * 2;
            return 0;
    }
}

bool test_buffer_logic_simple_message() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_logic_simple_message");
    
    Buffer b;
    buffer_init(&b);

    // Подменяем функцию протокола
    buffer_logic_set_expected_size_resolver(mock_expected_size);

    // Сначала записываем тип сообщения
    uint8_t type = CLIENT_STREAM_CREATE;  // Используем реальный тип
    TEST_ASSERT(&ctx, buffer_write(&b, &type, 1) == BUFFER_IS_INCOMPLETE, "Should be incomplete after writing type");

    // Теперь устанавливаем expected_size на основе типа
    TEST_ASSERT(&ctx, buffer_protocol_set_expected(&b) == 0, "Should set expected size successfully");
    TEST_ASSERT(&ctx, b.expected_size == 1 + sizeof(StreamCreatePayload), "Expected size should match StreamCreatePayload size");

    TEST_REPORT(&ctx, "test_buffer_logic_simple_message");
}

bool test_buffer_logic_incomplete_message() {
    TestContext ctx;
    TEST_INIT(&ctx, "test_buffer_logic_incomplete_message");
    
    Buffer b;
    buffer_init(&b);

    buffer_logic_set_expected_size_resolver(mock_expected_size);

    // Записываем тип сообщения
    uint8_t type = CLIENT_STREAM_DELETE;
    TEST_ASSERT(&ctx, buffer_write(&b, &type, 1) == BUFFER_IS_INCOMPLETE, "Should be incomplete after writing type");
    
    // Устанавливаем expected_size на основе типа
    TEST_ASSERT(&ctx, buffer_protocol_set_expected(&b) == 0, "Should set expected size");
    uint32_t expected_size = 1 + sizeof(StreamIDPayload);
    TEST_ASSERT(&ctx, b.expected_size == expected_size, "Expected size should match StreamIDPayload size");
    TEST_ASSERT(&ctx, b.position == 1, "Position should be 1 (type byte)");

    // Записываем неполные данные
    StreamIDPayload partial_payload = {300};
    uint32_t partial_size = sizeof(StreamIDPayload) - 1;  // Не хватает 1 байта
    TEST_ASSERT(&ctx, buffer_write(&b, &partial_payload, partial_size) == BUFFER_IS_INCOMPLETE, "Should be incomplete after partial data");
    TEST_ASSERT(&ctx, b.position == 1 + partial_size, "Position should be 1 + partial_size");
    TEST_ASSERT(&ctx, buffer_protocol_state(&b) == BUFFER_IS_INCOMPLETE, "Protocol state should be incomplete");

    // Дописываем последний байт
    uint8_t final_byte = ((uint8_t*)&partial_payload)[partial_size];
    TEST_ASSERT(&ctx, buffer_write(&b, &final_byte, 1) == BUFFER_IS_COMPLETE, "Should be complete after all data");
    TEST_ASSERT(&ctx, b.position == expected_size, "Position should match expected size");
    TEST_ASSERT(&ctx, buffer_protocol_state(&b) == BUFFER_IS_COMPLETE, "Protocol state should be complete");

    TEST_REPORT(&ctx, "test_buffer_logic_incomplete_message");
}
// Функция для запуска всех тестов буфера
bool run_all_buffer_tests() {
    printf("Running buffer tests...\n");
    
    bool all_passed = true;
    all_passed = test_buffer_init() && all_passed;
    all_passed = test_buffer_reserve() && all_passed;
    all_passed = test_buffer_write_basic() && all_passed;
    all_passed = test_buffer_write_overflow() && all_passed;
    all_passed = test_buffer_read_basic() && all_passed;
    all_passed = test_buffer_read_with_expected() && all_passed;
    all_passed = test_buffer_read_overflow() && all_passed;
    all_passed = test_buffer_state() && all_passed;
    all_passed = test_buffer_logic_simple_message() && all_passed;
    all_passed = test_buffer_logic_incomplete_message() && all_passed;
    
    if (all_passed) {
        printf("All buffer tests completed successfully! ✓\n\n");
    } else {
        printf("Some buffer tests failed! ✗\n\n");
    }
    
    return all_passed;
}