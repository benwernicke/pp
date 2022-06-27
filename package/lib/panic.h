#ifndef PANIC_H
#define PANIC_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define panic(...)                    \
    {                                 \
        fprintf(stderr, "ERROR: ");   \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
        exit(1);                      \
    }

#define panic_if(cond, ...)     \
    {                           \
        if (cond) {             \
            panic(__VA_ARGS__); \
        }                       \
    }

#endif
