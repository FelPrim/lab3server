// test/test_common.h
#pragma once

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

typedef struct {
    char message[1024];
    bool failed;
} TestContext;

#define TEST_INIT(ctx, test_name) do { \
    (ctx)->message[0] = '\0'; \
    (ctx)->failed = false; \
} while(0)

#define TEST_FAIL(ctx, fmt, ...) do { \
    snprintf((ctx)->message, sizeof((ctx)->message), "FAIL: " fmt, ##__VA_ARGS__); \
    (ctx)->failed = true; \
} while(0)

#define TEST_ASSERT(ctx, condition, fmt, ...) do { \
    if (!(condition)) { \
        TEST_FAIL(ctx, fmt, ##__VA_ARGS__); \
        return false; \
    } \
} while(0)

#define TEST_REPORT(ctx, test_name) do { \
    if ((ctx)->failed) { \
        printf("=== Test Failed: %s ===\n", test_name); \
        printf("%s\n", (ctx)->message); \
        return false; \
    } else { \
        return true; \
    } \
} while(0)