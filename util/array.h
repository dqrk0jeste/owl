#include <stdlib.h>

// clang-format off
#ifndef ARRAY_INITIAL_CAP
#define ARRAY_INITIAL_CAP 16
#endif

#define array_len(arr) (*((size_t *)(arr) - 1))
#define array_cap(arr) (*((size_t *)(arr) - 2))

#define array_last(arr) (arr + array_len(arr) - 1)
#define array_index_of(arr, ptr) (((ptr) < (arr) || (ptr) > array_last(arr)) ? -1 : (ptr) - (arr))
#define array_prev(arr, ptr) ((ptr) == (arr) ? NULL : (ptr) - 1)
#define array_next(arr, ptr) ((ptr) == (array_last(arr)) ? NULL : (ptr) + 1)

#define array_alloc_start(arr) ((size_t *)(arr) - 2)

#define array_init(arr) do {                                                                                         \
        *(arr) = (typeof(*(arr)))((size_t *)(malloc(2 * sizeof(size_t) + ARRAY_INITIAL_CAP * sizeof(**(arr)))) + 2); \
        array_len(*(arr)) = 0;                                                                                       \
        array_cap(*(arr)) = ARRAY_INITIAL_CAP;                                                                       \
    } while(0)

#define array_destroy(arr) do {   \
    free(array_alloc_start(arr)); \
} while(0)

#define array_push(arr, el) do {                                                 \
    if(array_len(*(arr)) == array_cap(*(arr))) {                                 \
        array_cap(*(arr)) *= 2;                                                  \
        *(arr) = (typeof(*(arr)))((size_t *)(realloc(array_alloc_start(*(arr)),  \
                2 * sizeof(size_t) + array_cap(*(arr)) * sizeof(**(arr)))) + 2); \
    }                                                                            \
    (*(arr))[array_len(*(arr))] = (el);                                          \
    array_len(*(arr))++;                                                         \
} while(0)

#define array_remove(arr, idx) do {                               \
    for(size_t __i = (idx); __i < array_len(*(arr)) - 1; __i++) { \
            (*(arr))[__i] = (*(arr))[__i + 1];                    \
    }                                                             \
    array_len(*(arr))--;                                          \
} while(0)

#define array_remove_by_ptr(arr, ptr) do {                                    \
    for(typeof(*(arr)) __ptr = (ptr); __ptr < array_last(*(arr)); __ptr++) {  \
        *__ptr = *(__ptr + 1);                                                \
    }                                                                         \
    array_len(*(arr))--;                                                      \
} while(0)

#define array_insert(arr, idx, el) do {                                          \
    if(array_len(*(arr)) == array_cap(*(arr))) {                                 \
        array_cap(*(arr)) *= 2;                                                  \
        *(arr) = (typeof(*(arr)))((size_t *)(realloc(array_alloc_start(*(arr)),  \
                2 * sizeof(size_t) + array_cap(*(arr)) * sizeof(**(arr)))) + 2); \
    }                                                                            \
    for(ssize_t __i = array_len(*(arr)) - 1; __i >= (idx); __i--) {              \
        (*(arr))[__i + 1] = (*(arr))[__i];                                       \
    }                                                                            \
    (*(arr))[(idx)] = (el);                                                      \
    array_len(*(arr))++;                                                         \
} while(0)

#define array_insert_after(arr, after, el) do {                                  \
    if(array_len(*(arr)) == array_cap(*(arr))) {                                 \
        array_cap(*(arr)) *= 2;                                                  \
        *(arr) = (typeof(*(arr)))((size_t *)(realloc(array_alloc_start(*(arr)),  \
                2 * sizeof(size_t) + array_cap(*(arr)) * sizeof(**(arr)))) + 2); \
    }                                                                            \
    for(typeof(*(arr)) __ptr = array_last(*(arr)); __ptr > (after); __ptr--) {   \
        *(__ptr + 1) = *__ptr;                                                   \
    }                                                                            \
    *(after + 1) = (el);                                                         \
    array_len(*(arr))++;                                                         \
} while(0)

#define array_insert_before(arr, before, el) do {                                \
    if(array_len(*(arr)) == array_cap(*(arr))) {                                 \
        array_cap(*(arr)) *= 2;                                                  \
        *(arr) = (typeof(*(arr)))((size_t *)(realloc(array_alloc_start(*(arr)),  \
                2 * sizeof(size_t) + array_cap(*(arr)) * sizeof(**(arr)))) + 2); \
    }                                                                            \
    for(typeof(*(arr)) __ptr = array_last(*(arr)); __ptr >= (before); __ptr--) { \
        *(__ptr + 1) = *__ptr;                                                   \
    }                                                                            \
    *(before) = (el);                                                            \
    array_len(*(arr))++;                                                         \
} while(0)

// clang-format on
