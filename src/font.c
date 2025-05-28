#include "font.h"

#include <stdio.h>
#include <string.h>

#include "array.h"

void
font_manager_init(void) {
    fcft_init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_INFO);
}

void
font_manager_deinit(void) {
    fcft_fini();
}

struct font *
font_create(char *name, int size) {
    struct font *font = calloc(1, sizeof(*font));
    font->name = strdup(name);
    font->size = size;
    array_init(&font->scales);

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

static struct fcft_font *
create_at_scale(struct font *font, float scale) {
    char pixelsize[32];
    snprintf(pixelsize, sizeof(pixelsize), "pixelsize=%d", (int)(font->size * scale));

    struct font_scale at_scale = {scale, fcft_from_name(1, (const char **)&font->name, pixelsize)};
    if(at_scale.font != NULL) {
        array_push(&font->scales, at_scale);
    }

    return at_scale.font;
}

struct fcft_font *
font_get_at_scale(struct font *font, float scale) {
    for(struct font_scale *iter = font->scales; iter <= array_last(font->scales); iter++) {
        if(iter->scale == scale) {
            return iter->font;
        }
    }

    return create_at_scale(font, scale);  // may be NULL
}
