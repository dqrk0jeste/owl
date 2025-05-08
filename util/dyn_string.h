#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// initial capacity of strings; you should keep this a power of two
#ifndef STRING_DEFAULT_CAP
#define STRING_DEFAULT_CAP 16
#endif

// initialize a new string with the value of a null-terminated c-string s; if NULL is passed, creates an empty string
char *
string_new(char *s);

char *
string_new_with_capacity(char *s, size_t cap);

char *
string_new_empty(void);

char *
string_new_empty_with_capacity(size_t cap);

void
string_destroy(char *s);

size_t
string_len(char *s);

size_t
string_cap(char *s);

char *
string_clone(char *s);

// check if the two provided strings `a` and `b` are the same; note: even tho you can use `strcmp()`, you are
// advised to use this function, since it will skip the comparasion if their lengths are not the same. it still
// uses `strcmp()` under the hood, so there isn't a speed tradeoff at all
bool
string_equal(char *a, char *b);

// concatinates two strings a and b and returns it as a new allocated string
char *
string_concat(char *a, char *b);

// find first occurrence of character `c` in string `s` after `start`, -1 for no
// such char in the string
int
string_index_of(char *s, char c, size_t start);

// find any char specified in the c-string `c`. returning the index and which char was found in `*which`; set `which` to
// NULL to dismiss
int32_t
string_index_of_any(char *s, char *c, size_t start, char *which);

// returns a new substring starting at `start` index and until `end` (not inlcuding `end`)
char *
string_substring(char *s, size_t start, size_t end);

// these next few function take a pointer to your variable, because they may perform reallocations
void
string_append(char **s, char c);

void
string_append_string(char **s, char *append);

void
string_append_c_string(char **s, char *append);

#ifdef STRING_IMPLEMENTATION

// internal functions, you should not call these directly
static void
__set_len(char *s, size_t len) {
    *((size_t *)s - 1) = len;
}

static void
__set_cap(char *s, size_t cap) {
    *((size_t *)s - 2) = cap;
}

static void *
__alloc_start(char *s) {
    return (size_t *)s - 2;
}

static void
__grow_cap(char **s, size_t cap) {
    size_t *tmp = (size_t *)realloc(__alloc_start(*s), 2 * sizeof(size_t) + cap);

    // patch the new capacity
    *tmp = cap;

    tmp += 2;
    *s = (char *)tmp;
}

char *
string_new_empty_with_capacity(size_t cap) {
    size_t *meta = (size_t *)malloc(2 * sizeof(size_t) + cap);

    // capacity
    *meta = cap;
    // length
    *(++meta) = 0;

    // null terminate the string
    char *ret = (char *)++meta;
    *ret = 0;

    return ret;
}

char *
string_new_empty(void) {
    return string_new_empty_with_capacity(STRING_DEFAULT_CAP);
}

char *
string_new_with_capacity(char *s, size_t cap) {
    if(s == NULL)
        return string_new_empty_with_capacity(cap);

    size_t len = strlen(s);
    cap = len + 1 > cap ? len + 1 : cap;

    size_t *meta = (size_t *)malloc(2 * sizeof(size_t) + cap);

    *meta = cap;
    *(++meta) = len;
    // we copy the string (as well as the null terminator)
    memcpy(++meta, s, len + 1);

    return (char *)meta;
}

char *
string_new(char *s) {
    return string_new_with_capacity(s, STRING_DEFAULT_CAP);
}

void
string_destroy(char *s) {
    free((size_t *)s - 2);
}

size_t
string_len(char *s) {
    return *((size_t *)s - 1);
}

size_t
string_cap(char *s) {
    return *((size_t *)s - 2);
}

char *
string_clone(char *s) {
    char *ret = string_new_empty_with_capacity(string_len(s) + 1);
    memcpy(ret, s, string_len(s) + 1);
    __set_len(ret, string_len(s));

    return ret;
}

bool
string_equal(char *a, char *b) {
    size_t len_a = string_len(a);
    size_t len_b = string_len(b);

    if(len_a != len_b)
        return false;

    return strcmp(a, b) == 0;
}

char *
string_concat(char *a, char *b) {
    size_t len_a = string_len(a);
    size_t len_b = string_len(b);

    char *c = string_new_with_capacity(NULL, len_a + len_b + 1);
    __set_len(c, len_a + len_b);

    memcpy(c, a, len_a);
    memcpy(c + len_a, b, len_b);
    c[len_a + len_b] = 0;

    return c;
}

int
string_index_of(char *s, char c, size_t start) {
    for(size_t i = start; i < string_len(s); i++) {
        if(s[i] == c)
            return i;
    }

    return -1;
}

int
string_index_of_any(char *s, char *c, size_t start, char *which) {
    for(size_t i = start; i < string_len(s); i++) {
        char *t = c;
        while(*t != 0) {
            if(s[i] == *t) {
                if(which != NULL) {
                    *which = *t;
                }
                return i;
            }
            t++;
        }
    }

    return -1;
}

char *
string_substring(char *s, size_t start, size_t end) {
    size_t len = string_len(s);
    if(end > len)
        end = len;
    if(start > end)
        return string_new_empty();

    char *sub = string_new_with_capacity(NULL, end - start + 1);
    memcpy(sub, s + start, end - start);
    sub[end - start] = 0;
    __set_len(sub, end - start);

    return sub;
}

void
string_append(char **s, char c) {
    size_t len = string_len(*s);
    if(len + 1 == string_cap(*s)) {
        __grow_cap(s, 2 * string_cap(*s));
    }

    (*s)[len] = c;
    (*s)[len + 1] = 0;
    __set_len(*s, len + 1);
}

void
string_append_string(char **s, char *append) {
    size_t new_len = string_len(*s) + string_len(append);
    if(string_cap(*s) <= new_len) {
        size_t new_cap = 2 * string_cap(*s);
        while(new_cap < new_len)
            new_cap *= 2;

        __grow_cap(s, new_cap);
    }

    memcpy(*s + string_len(*s), append, string_len(append) + 1);
    __set_len(*s, new_len);
}

void
string_append_c_string(char **s, char *append) {
    while(*append != 0)
        string_append(s, *(append++));
}

#endif
