#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "array.h"

// parse the provided string, that ends with `end` (to be compilient with e.g. `fgets()`), into a series of words, with
// the ability to use "" to include whitespace inside a word, and \ to escape the next character inside a word.
//
// manipulates the original string, returning an array of pointers in the original string to null-terminated words.
// caller needs to destroy the array by calling `array_destroy()` after he is done with it.
char **
parser_into_words(char *s, char end);

enum parser_line_type {
    PARSER_LINE_TYPE_EOF = 0,
    PARSER_LINE_TYPE_EMPTY,
    PARSER_LINE_TYPE_COMMENT,
    PARSER_LINE_TYPE_SECTION,
    PARSER_LINE_TYPE_WORDS,
};

struct parser_line {
    enum parser_line_type type;
    union {
        char *section;
        char **words;
    };
};

// parses the next line in the file, return the type of that word and parsed result (if any). returns `parser_line` of
// type `PARSER_LINE_TYPE_EOF` on end-of-file. if the type is `PARSER_LINE_TYPE_WORDS`, caller needs to call
// `array_destroy()` to destroy the returned array of words
struct parser_line
parser_parse_next_line(FILE *file, char *buffer, size_t size);

#ifdef PARSER_IMPLEMENTATION

static inline bool
__is_whitespace(char c) {
    return c == ' ' || c == '\t';
}

static char *
__find_closing_quote_or_end(char **start, char end) {
    (*start)++;
    char *p = *start;

    bool escape_next = false;
    while(true) {
        if(*p == end || (!escape_next && *p == '"'))
            return p;

        if(escape_next) {
            escape_next = false;
        } else if(*p == '\\') {
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
parser_into_words(char *s, char end) {
    char **words;
    array_init(&words);

    while(true) {
        while(__is_whitespace(*s))
            s++;

        if(*s == end)
            break;

        char *word_start = s;
        char *word_end =
                *s == '"' ? __find_closing_quote_or_end(&word_start, end) : __find_whitespace_or_end(word_start, end);

        if(*word_end == end) {
            *word_end = 0;
            array_push(&words, word_start);
            break;
        }

        *word_end = 0;
        array_push(&words, word_start);
        s = word_end + 1;
    }

    return words;
}

static inline char *
__find_closing_bracket_or_end(char *start) {
    while(*start != ']' && *start != '\n')
        start++;

    return start;
}

struct parser_line
parser_next_line(FILE *file, char *buffer, size_t size) {
    if(fgets(buffer, size, file) == NULL) {
        return (struct parser_line){.type = PARSER_LINE_TYPE_EOF};
    }

    char *p = buffer;
    while(__is_whitespace(*p))
        p++;

    struct parser_line line;
    if(*p == '\n') {
        line.type = PARSER_LINE_TYPE_EMPTY;
    } else if(*p == '#') {
        line.type = PARSER_LINE_TYPE_COMMENT;
    } else if(*p == '[') {
        char *start = p + 1;
        char *end = __find_closing_bracket_or_end(start);
        *end = 0;

        line.type = PARSER_LINE_TYPE_SECTION;
        line.section = start;
    } else {
        line.type = PARSER_LINE_TYPE_WORDS;
        line.words = parser_into_words(buffer, '\n');
    }

    return line;
}

#endif
