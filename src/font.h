#pragma once

#include <fcft/fcft.h>

struct font_scale {
    float scale;
    struct fcft_font *font;
};

struct font {
    int names_count;
    char **names;
    int size;

    struct font_scale *scales;  // array
};

// needs to be called before calling any of the later functions
void
font_manager_init(void);

void
font_manager_deinit(void);

// returns NULL if the font could not be loaded
struct font *
font_create(int count, char *names[static count], int size);

void
font_destroy(struct font *font);

// returns an already existing font or creates it if needed
struct fcft_font *
font_get_at_scale(struct font *font, float scale);
