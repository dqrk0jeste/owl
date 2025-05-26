#include "font.h"

#include <stdio.h>
#include <string.h>

#include "array.h"

static struct font_manager {
} manager;

void
font_manager_init(void) {
    fcft_init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_INFO);
}

void
font_manager_deinit(void) {
    fcft_fini();
}

struct font *
font_load(char *name, int size, int scale_count, float scales[static scale_count]) {
    struct font *font = calloc(1, sizeof(*font));
    font->name = strdup(name);
    font->size = size;

    array_init(&font->scales);
    for(int i = 0; i < scale_count; i++) {
        char pixelsize[32];
        snprintf(pixelsize, sizeof(pixelsize), "pixelsize=%d", (int)(size * scales[i]));

        struct font_scale at_scale = {scales[i], fcft_from_name(1, (const char **)&name, pixelsize)};
        if(at_scale.font != NULL) {
            array_push(&font->scales, at_scale);
        }
    }

    return font;
}

void
font_destroy(struct font *font) {
    for(struct font_scale *iter = font->scales; iter <= array_last(font->scales); iter++) {
        fcft_destroy(iter->font);
    }
    array_destroy(font->scales);

    free(font->name);
    free(font);
}

// returns NULL if there is no such font
struct fcft_font *
font_get_at_scale(struct font *font, float scale) {
    for(struct font_scale *iter = font->scales; iter <= array_last(font->scales); iter++) {
        if(iter->scale == scale) {
            return iter->font;
        }
    }

    return NULL;
}

void
font_add_scale(struct font *font, float scale) {
    char pixelsize[32];
    snprintf(pixelsize, sizeof(pixelsize), "pixelsize=%d", (int)(font->size * scale));

    struct font_scale at_scale = {scale, fcft_from_name(1, (const char **)&font->name, pixelsize)};
    if(at_scale.font != NULL) {
        array_push(&font->scales, at_scale);
    }
}

void
font_remove_scale(struct font *font, float scale) {
    for(int i = 0; i < array_len(font->scales); i++) {
        if(font->scales[i].scale == scale) {
            array_remove(&font->scales, i);
            i--;
        }
    }
}
