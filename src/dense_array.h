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

// ИСПРАВЛЕННЫЙ макрос для удаления элемента из плотного массива
// Просто устанавливает элемент в NULL без сдвига - это безопаснее
#define DENSE_ARRAY_REMOVE(array, size, element) ({ \
    int _result = -1; \
    for (size_t i = 0; i < (size); ++i) { \
        if ((array)[i] == (element)) { \
            (array)[i] = NULL; \
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

// ДОБАВЛЕН: Макрос для получения индекса элемента
#define DENSE_ARRAY_INDEX_OF(array, size, element) ({ \
    int _index = -1; \
    for (size_t i = 0; i < (size); ++i) { \
        if ((array)[i] == (element)) { \
            _index = (int)i; \
            break; \
        } \
    } \
    _index; \
})

// ДОБАВЛЕН: Макрос для безопасной итерации по ненулевым элементам
#define DENSE_ARRAY_FOREACH(array, size, item, code) do { \
    for (size_t i = 0; i < (size); ++i) { \
        if ((array)[i] != NULL) { \
            typeof((array)[0]) item = (array)[i]; \
            code; \
        } \
    } \
} while(0)