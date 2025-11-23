#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

// Макрос для инициализации плотного массива указателей
#define DENSE_ARRAY_INIT(array, size) do { \
    for (size_t i = 0; i < (size); ++i) { \
        (array)[i] = NULL; \
    } \
} while(0)

// Макрос для добавления элемента в плотный массив
// Возвращает индекс, куда был добавлен элемент, или -1 при ошибке
#define DENSE_ARRAY_ADD(array, size, element) ({ \
    int _index = -1; \
    for (size_t i = 0; i < (size); ++i) { \
        if ((array)[i] == NULL) { \
            (array)[i] = (element); \
            _index = (int)i; \
            break; \
        } \
    } \
    _index; \
})

// Макрос для удаления элемента из плотного массива
// Возвращает 0 при успехе, -1 если элемент не найден
#define DENSE_ARRAY_REMOVE(array, size, element) ({ \
    int _result = -1; \
    for (size_t i = 0; i < (size); ++i) { \
        if ((array)[i] == (element)) { \
            /* Сдвигаем все последующие элементы */ \
            for (size_t j = i; j + 1 < (size); ++j) { \
                (array)[j] = (array)[j + 1]; \
            } \
            (array)[(size) - 1] = NULL; \
            _result = 0; \
            break; \
        } \
    } \
    _result; \
})

// Макрос для проверки наличия элемента в плотном массиве
#define DENSE_ARRAY_CONTAINS(array, size, element) ({ \
    bool _found = false; \
    for (size_t i = 0; i < (size); ++i) { \
        if ((array)[i] == (element)) { \
            _found = true; \
            break; \
        } \
    } \
    _found; \
})

// Макрос для получения количества элементов в плотном массиве
#define DENSE_ARRAY_COUNT(array, size) ({ \
    size_t _count = 0; \
    for (size_t i = 0; i < (size); ++i) { \
        if ((array)[i] != NULL) { \
            _count++; \
        } \
    } \
    _count; \
})

// Макрос для поиска элемента по предикату
#define DENSE_ARRAY_FIND_BY(array, size, predicate, result) ({ \
    int _index = -1; \
    for (size_t i = 0; i < (size); ++i) { \
        if ((array)[i] != NULL && (predicate((array)[i]))) { \
            *(result) = (array)[i]; \
            _index = (int)i; \
            break; \
        } \
    } \
    _index; \
})
