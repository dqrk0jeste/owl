#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "array.h"

// parse the provided string, that ends with `end` (to be compilient with e.g. `fgets()`), into a series of words,
// with the ability to use quotes to include whitespace inside a word, and `escape` to escape the `quote` or `escape`
// inside a word.
//
// manipulates the original string, returning an array of pointers in the original string to null-terminated words.
// caller needs to destroy the array by calling `array_destroy()`.
char **
parse_string(char *s, char quote, char escape, char end);

#ifdef PARSER_IMPLEMENTATION

static inline bool
__is_whitespace(char c) {
    return c == ' ' || c == '\t';
}

static char *
__find_closing_quote_or_end(char **start, char quote, char escape, char end) {
    (*start)++;
    char *p = *start;

    bool escape_next = false;
    while(true) {
        if(*p == end || (!escape_next && *p == quote))
            return p;

        if(escape_next) {
            escape_next = false;
        } else if(*p == escape) {
            escape_next = true;
            memmove(*start + 1, *start, p - *start);
            (*start)++;
        }

        p++;
    }
}

static inline char *
__find_whitespace_or_end(char *start, char end) {
    while(*start != end && !__is_whitespace(*start))
        start++;

    return start;
}

char **
parse_string(char *s, char quote, char escape, char end) {
    char **words;
    array_init(&words);

    char *p = s;
    while(true) {
        while(__is_whitespace(*p))
            p++;

        if(*p == end)
            break;

        char *word_start = p;
        char *word_end = *p == quote ? __find_closing_quote_or_end(&word_start, quote, escape, end)
                                     : __find_whitespace_or_end(word_start, end);

        if(*word_end == end) {
            *word_end = 0;
            array_push(&words, word_start);
            break;
        }

        *word_end = 0;
        array_push(&words, word_start);
        p = word_end + 1;
    }

    return words;
}

#endif
