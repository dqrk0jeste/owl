#pragma once

#include <stdlib.h>
#include <string.h>

#define ARRAY_INITIAL_CAP 16

#define array_len(arr) (*((size_t *)(arr) - 1))
#define array_cap(arr) (*((size_t *)(arr) - 2))
#define array_alloc_start(arr) ((size_t *)(arr) - 2)

#define array_init(arr)                                                                                              \
    {                                                                                                                \
        *(arr) = (typeof(*(arr)))((size_t *)(malloc(2 * sizeof(size_t) + ARRAY_INITIAL_CAP * sizeof(**(arr)))) + 2); \
        array_len(*(arr)) = 0;                                                                                       \
        array_cap(*(arr)) = ARRAY_INITIAL_CAP;                                                                       \
    }

#define array_destroy(arr)               \
    {                                    \
        free(array_alloc_start(*(arr))); \
    }

#define array_push(arr, el)                                                                                \
    {                                                                                                      \
        if(array_len(*(arr)) == array_cap(*(arr))) {                                                       \
            array_cap(*(arr)) *= 2;                                                                        \
            *(arr) = (typeof(*(arr)))((size_t *)(realloc(array_alloc_start(*(arr)),                        \
                                              2 * sizeof(size_t) + array_cap(*(arr)) * sizeof(**(arr)))) + \
                    2);                                                                                    \
        }                                                                                                  \
        (*(arr))[array_len(*(arr))] = (el);                                                                \
        array_len(*(arr))++;                                                                               \
    }

#define array_remove(arr, idx)                                        \
    {                                                                 \
        for(size_t __i = (idx); __i < array_len(*(arr)) - 1; __i++) { \
            (*(arr))[__i] = (*(arr))[__i + 1];                        \
        }                                                             \
        array_len(*(arr))--;                                          \
    }

#define array_insert(arr, idx, el)                                                                         \
    {                                                                                                      \
        if(array_len(*(arr)) == array_cap(*(arr))) {                                                       \
            array_cap(*(arr)) *= 2;                                                                        \
            *(arr) = (typeof(*(arr)))((size_t *)(realloc(array_alloc_start(*(arr)),                        \
                                              2 * sizeof(size_t) + array_cap(*(arr)) * sizeof(**(arr)))) + \
                    2);                                                                                    \
        }                                                                                                  \
        for(ssize_t __i = array_len(*(arr)) - 1; __i >= (idx); __i--) {                                    \
            (*(arr))[__i + 1] = (*(arr))[__i];                                                             \
        }                                                                                                  \
        (*(arr))[(idx)] = (el);                                                                            \
        array_len(*(arr))++;                                                                               \
    }

#define array_clone(dest, src)                                                                                        \
    {                                                                                                                 \
        *(dest) = (typeof(*(src)))((size_t *)(malloc(2 * sizeof(size_t) + array_len(*(src)) * sizeof(**(src)))) + 2); \
        memcpy(*(dest), *(src), array_len(*(src)) * sizeof(**(src)));                                                 \
        array_len(*(dest)) = array_len(*(src));                                                                       \
        array_cap(*(dest)) = array_len(*(src));                                                                       \
    }
