#ifndef _CABBAGE_UTIL_H
#define _CABBAGE_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

inline static const char* errmsg() {
    return strerror(errno);
}

#define die(fmt, ...) do { \
    LOG(FATAL, fmt, ##__VA_ARGS__); \
    exit(1); \
} while (0)

#define edie(fmt, ...) die(fmt ": %s", ##__VA_ARGS__, errmsg())

#endif 
