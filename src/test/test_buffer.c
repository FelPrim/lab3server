#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../buffer.h"
#include "../buffer_logic.h"

// Объявление функции, которая определена в buffer_logic.c но не в header'е
void buffer_logic_set_expected_size_resolver(int (*resolver)(uint8_t, uint32_t*));

void test_buffer_init() {
    Buffer b;
    buffer_init(&b);
    assert(b.position == 0);
    assert(b.expected_size == 0);
    printf("✓ test_buffer_init passed\n");
}

void test_buffer_reserve() {
    Buffer b;
    buffer_init(&b);

    assert(buffer_reserve(&b, 10) == 0);
    assert(b.position == 0);
    assert(b.expected_size == 10);

    // Неверный вызов - слишком большой размер
    assert(buffer_reserve(&b, BUFFER_SIZE + 1) != 0);

    printf("✓ test_buffer_reserve passed\n");
}

void test_buffer_write_basic() {
    Buffer b;
    buffer_init(&b);

    buffer_reserve(&b, 8);

    uint8_t d1[5] = {1,2,3,4,5};
    assert(buffer_write(&b, d1, 5) == BUFFER_IS_INCOMPLETE);
    assert(b.position == 5);
    assert(b.expected_size == 8);

    uint8_t d2[3] = {6,7,8};
    assert(buffer_write(&b, d2, 3) == BUFFER_IS_COMPLETE);
    assert(b.position == 8);
    // expected_size НЕ должен сбрасываться в 0 после полной записи
    assert(b.expected_size == 8);

    printf("✓ test_buffer_write_basic passed\n");
}

void test_buffer_write_overflow() {
    Buffer b;
    buffer_init(&b);

    buffer_reserve(&b, 5);

    uint8_t d1[6] = {1,2,3,4,5,6};
    
    // Слишком много данных - должно вернуть OVERFLOW
    assert(buffer_write(&b, d1, 6) == BUFFER_OVERFLOW);
    assert(b.position == 0);  // Позиция не должна измениться
    assert(b.expected_size == 5);

    printf("✓ test_buffer_write_overflow passed\n");
}

void test_buffer_read_basic() {
    Buffer b;
    buffer_init(&b);

    // Записываем данные без использования expected_size для простоты
    uint8_t src[8] = {10,20,30,40,50,60,70,80};
    assert(buffer_write(&b, src, 8) == BUFFER_IS_INCOMPLETE);
    assert(b.position == 8);

    // Читаем 5 байт
    uint8_t out1[5];
    // Без expected_size всегда возвращает INCOMPLETE
    assert(buffer_read(&b, out1, 5) == BUFFER_IS_INCOMPLETE);
    assert(b.position == 3);
    assert(memcmp(out1, (uint8_t[]){10,20,30,40,50}, 5) == 0);

    // Читаем оставшиеся 3 байта
    uint8_t out2[3];
    // Без expected_size всегда возвращает INCOMPLETE
    assert(buffer_read(&b, out2, 3) == BUFFER_IS_INCOMPLETE);
    assert(b.position == 0);
    assert(memcmp(out2, (uint8_t[]){60,70,80}, 3) == 0);

    printf("✓ test_buffer_read_basic passed\n");
}

void test_buffer_read_with_expected() {
    Buffer b;
    buffer_init(&b);

    // Устанавливаем expected_size и записываем данные
    buffer_reserve(&b, 8);
    uint8_t src[8] = {10,20,30,40,50,60,70,80};
    assert(buffer_write(&b, src, 8) == BUFFER_IS_COMPLETE);
    assert(b.position == 8);
    assert(b.expected_size == 8);

    // Читаем 5 байт
    uint8_t out1[5];
    // После чтения 5 байт, expected_size становится 3, position становится 3
    // position == expected_size, поэтому состояние COMPLETE
    assert(buffer_read(&b, out1, 5) == BUFFER_IS_COMPLETE);
    assert(b.position == 3);
    assert(b.expected_size == 3);
    assert(memcmp(out1, (uint8_t[]){10,20,30,40,50}, 5) == 0);

    // Читаем оставшиеся 3 байта
    uint8_t out2[3];
    // После чтения 3 байт, expected_size становится 0, position становится 0
    // expected_size == 0, поэтому состояние INCOMPLETE
    assert(buffer_read(&b, out2, 3) == BUFFER_IS_INCOMPLETE);
    assert(b.position == 0);
    assert(b.expected_size == 0);
    assert(memcmp(out2, (uint8_t[]){60,70,80}, 3) == 0);

    printf("✓ test_buffer_read_with_expected passed\n");
}

void test_buffer_read_overflow() {
    Buffer b;
    buffer_init(&b);

    // Записываем 5 байт
    uint8_t data[5] = {1,2,3,4,5};
    assert(buffer_write(&b, data, 5) == BUFFER_IS_INCOMPLETE);
    assert(b.position == 5);

    uint8_t out[10];
    memset(out, 0xAA, sizeof(out));

    // Пытаемся прочитать больше чем есть
    assert(buffer_read(&b, out, 10) == BUFFER_OVERFLOW);
    assert(b.position == 5);  // Позиция не должна измениться

    // Проверяем что буфер назначения не изменился
    for (int i = 0; i < 10; i++) {
        assert(out[i] == 0xAA);
    }

    printf("✓ test_buffer_read_overflow passed\n");
}

void test_buffer_state() {
    Buffer b;
    buffer_init(&b);

    // Тестируем без expected_size
    assert(buffer_state(&b) == BUFFER_IS_INCOMPLETE);

    // Добавляем данные
    uint8_t data[3] = {1,2,3};
    buffer_write(&b, data, 3);
    assert(buffer_state(&b) == BUFFER_IS_INCOMPLETE);

    // Тестируем с expected_size
    buffer_clear(&b);
    buffer_reserve(&b, 4);
    assert(buffer_state(&b) == BUFFER_IS_INCOMPLETE);

    buffer_write(&b, data, 2);
    assert(buffer_state(&b) == BUFFER_IS_INCOMPLETE);

    buffer_write(&b, data, 2);
    assert(buffer_state(&b) == BUFFER_IS_COMPLETE);

    printf("✓ test_buffer_state passed\n");
}

// Mock функция для тестов
static int mock_expected_size(uint8_t type, uint32_t* out) {
    *out = type * 2;
    return 0;
}

void test_buffer_logic_simple_message() {
    Buffer b;
    buffer_init(&b);

    // Подменяем функцию протокола
    buffer_logic_set_expected_size_resolver(mock_expected_size);

    // Сначала записываем тип сообщения
    uint8_t type = 3;
    assert(buffer_write(&b, &type, 1) == BUFFER_IS_INCOMPLETE);

    // Теперь устанавливаем expected_size на основе типа
    assert(buffer_protocol_set_expected(&b) == 0);
    assert(b.expected_size == 6);  // 3 * 2 = 6

    printf("✓ test_buffer_logic_simple_message passed\n");
}

void test_buffer_logic_incomplete_message() {
    Buffer b;
    buffer_init(&b);

    buffer_logic_set_expected_size_resolver(mock_expected_size);

    // Записываем тип сообщения
    uint8_t type = 3;
    assert(buffer_write(&b, &type, 1) == BUFFER_IS_INCOMPLETE);
    
    // Устанавливаем expected_size на основе типа (должен быть 6 байт)
    assert(buffer_protocol_set_expected(&b) == 0);
    assert(b.expected_size == 6);  // 3 * 2 = 6
    assert(b.position == 1);  // Уже записан 1 байт (тип)

    // Записываем неполные данные (4 байта из оставшихся 5)
    uint8_t data[4] = {1,2,3,4};
    assert(buffer_write(&b, data, 4) == BUFFER_IS_INCOMPLETE);
    assert(b.position == 5);  // 1 + 4 = 5 байт
    assert(buffer_protocol_state(&b) == BUFFER_IS_INCOMPLETE);

    // Дописываем последний 1 байт (всего 6 байт)
    uint8_t final_data[1] = {5};
    assert(buffer_write(&b, final_data, 1) == BUFFER_IS_COMPLETE);
    assert(b.position == 6);  // 1 + 4 + 1 = 6 байт
    assert(buffer_protocol_state(&b) == BUFFER_IS_COMPLETE);

    printf("✓ test_buffer_logic_incomplete_message passed\n");
}
// Функция для запуска всех тестов буфера
void run_all_buffer_tests() {
    printf("Running buffer tests...\n");
    test_buffer_init();
    test_buffer_reserve();
    test_buffer_write_basic();
    test_buffer_write_overflow();
    test_buffer_read_basic();
    test_buffer_read_with_expected();
    test_buffer_read_overflow();
    test_buffer_state();
    test_buffer_logic_simple_message();
    test_buffer_logic_incomplete_message();
    printf("All buffer tests completed successfully!\n\n");
}