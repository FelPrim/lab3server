#pragma once

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#ifndef EPFD_IS_DEFINED
#define EPFD_IS_DEFINED
int epfd;
#endif

#define IS_COMPLETED 0
#define ISNT_COMPLETED 1
#define CAUGHT_ERROR -1
