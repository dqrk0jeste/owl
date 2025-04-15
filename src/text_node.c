#include "text_node.h"

#include <assert.h>
#include <drm_fourcc.h>
#include <fcft/fcft.h>
#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <wayland-server-protocol.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>

#include "config.h"
#include "helpers.h"
#include "mwc.h"

extern struct mwc_server server;

static void
pixman_buffer_handle_destroy(struct wlr_buffer *wlr_buffer) {
    // i could not understand when this gets called, so i just clean it up myself
}

static bool
pixman_buffer_handle_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags, void **data, uint32_t *format,
        size_t *stride) {
    struct pixman_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);

    *data = pixman_image_get_data(buffer->image);
    *stride = pixman_image_get_stride(buffer->image);
    *format = DRM_FORMAT_ARGB8888;

    return true;
}

static void
pixman_buffer_handle_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
    // this space is intentionally left blank
}

static const struct wlr_buffer_impl pixman_buffer_impl = {
        .destroy = pixman_buffer_handle_destroy,
        .begin_data_ptr_access = pixman_buffer_handle_begin_data_ptr_access,
        .end_data_ptr_access = pixman_buffer_handle_end_data_ptr_access,
};

struct pixman_buffer *
pixman_buffer_create(uint32_t width, uint32_t height) {
    struct pixman_buffer *buffer = calloc(1, sizeof(*buffer));

    wlr_buffer_init(&buffer->base, &pixman_buffer_impl, width, height);

    buffer->width = width;
    buffer->height = height;
    buffer->image = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, NULL, 0);

    pixman_region32_t clip;
    pixman_region32_init_rect(&clip, 0, 0, width, height);
    pixman_image_set_clip_region32(buffer->image, &clip);
    pixman_region32_fini(&clip);

    return buffer;
}

void
pixman_buffer_destroy(struct pixman_buffer *buffer) {
    wlr_buffer_drop(&buffer->base);
    pixman_image_unref(buffer->image);

    free(buffer);
}

static uint32_t
render_glyphs_to_pixman_buffer(struct pixman_buffer *buffer, pixman_image_t *color, size_t count,
        const struct fcft_glyph *glyphs[static count], long kern[static count]) {
    long x = 0;

    for(size_t i = 0; i < count; i++) {
        const struct fcft_glyph *g = glyphs[i];
        if(g == NULL) continue;

        x += kern[i];

        pixman_image_composite32(PIXMAN_OP_OVER, color, g->pix, buffer->image, 0, 0, 0, 0, x + g->x,
                server.config->decoration.font->ascent - g->y, g->width, g->height);

        x += g->advance.x;
    }

    return x;
}

static uint32_t
render_chars_to_pixman_buffer(const char32_t *text, size_t len, struct pixman_buffer *buffer, pixman_image_t *color) {
    if(len == 0) return 0;

    const struct fcft_glyph *glyphs[len];
    long kern[len];

    for(size_t i = 0; i < len; i++) {
        glyphs[i] = fcft_rasterize_char_utf32(server.config->decoration.font, text[i], FCFT_SUBPIXEL_NONE);
        if(glyphs[i] == NULL) continue;

        kern[i] = 0;
        if(i > 0) {
            fcft_kerning(server.config->decoration.font, text[i - 1], text[i], &kern[i], NULL);
        }
    }

    return render_glyphs_to_pixman_buffer(buffer, color, len, glyphs, kern);
}

struct text_node *
text_node_create(struct wlr_scene_tree *parent, char *text) {
    assert(server.config->decoration.font);

    struct text_node *node = calloc(1, sizeof(*node));
    node->scene_buffer = wlr_scene_buffer_create(parent, NULL);

    text_node_set_text(node, text);

    return node;
}

void
text_node_destroy(struct text_node *node) {
    wlr_scene_buffer_set_buffer(node->scene_buffer, NULL);

    // we manually destroy the buffer since i didnt understand the mechanism in wlroots
    if(node->buffer != NULL) {
        pixman_buffer_destroy(node->buffer);
    }

    wlr_scene_node_destroy(&node->scene_buffer->node);

    free(node);
}

// function to decode a single utf8 character into a utf32 code point
static ssize_t
convert_utf8_to_utf32(char *utf8, char32_t *codepoint) {
    if((utf8[0] & 0x80) == 0x00) {
        *codepoint = utf8[0];
        return 1;
    } else if((utf8[0] & 0xE0) == 0xC0) {
        *codepoint = ((utf8[0] & 0x1F) << 6) | (utf8[1] & 0x3F);
        return 2;
    } else if((utf8[0] & 0xF0) == 0xE0) {
        *codepoint = ((utf8[0] & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
        return 3;
    } else if((utf8[0] & 0xF8) == 0xF0) {
        *codepoint = ((utf8[0] & 0x07) << 18) | ((utf8[1] & 0x3F) << 12) | ((utf8[2] & 0x3F) << 6) | (utf8[3] & 0x3F);
        return 4;
    } else {
        // invalid UTF-8
        return -1;
    }
}

void
text_node_set_text(struct text_node *node, char *text) {
    if(text == NULL) return;

    size_t len = strlen(text);

    // we approximate the width of the text
    uint32_t width = len * (server.config->decoration.font->max_advance.x);
    uint32_t height = server.config->decoration.font->max_advance.y;

    // todo: save an allocation if the current is bigger than this one; i dont care rn
    if(node->buffer != NULL) {
        pixman_buffer_destroy(node->buffer);
    }
    node->buffer = pixman_buffer_create(width, height);

    wlr_scene_buffer_set_buffer(node->scene_buffer, &node->buffer->base);

    // if the len is 0 then we attach the empty buffer
    if(len == 0) return;

    // convert the string to utf32
    // todo: optimize this so it just goes through the string once and just renders it char by char
    char32_t unicode[len];
    size_t i = 0, j = 0;
    while(i < len) {
        ssize_t move_forward = convert_utf8_to_utf32(&text[i], &unicode[j]);
        // if its invalid utf8 then we quit
        if(move_forward == -1) return;

        i += move_forward;
        j++;
    }

    pixman_color_t color;
    mwc_color_to_pixman_color(server.config->decoration.titlebar_title_color, &color);
    pixman_image_t *foreground_color = pixman_image_create_solid_fill(&color);

    node->width = render_chars_to_pixman_buffer(unicode, j, node->buffer, foreground_color);
    node->height = height;

    pixman_image_unref(foreground_color);
}
