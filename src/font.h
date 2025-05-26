#pragma once

#include <fcft/fcft.h>

struct font_scale {
    float scale;
    struct fcft_font *font;
};

struct font {
    char *name;
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
font_load(char *name, int size, int scale_count, float scales[static scale_count]);

void
font_destroy(struct font *font);

// returns NULL if there is no such font
struct fcft_font *
font_get_at_scale(struct font *font, float scale);

void
font_add_scale(struct font *font, float scale);

void
font_remove_scale(struct font *font, float scale);
