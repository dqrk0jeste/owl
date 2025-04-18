#include "text_node.h"

#include <assert.h>
#include <drm_fourcc.h>
#include <fcft/fcft.h>
#include <locale.h>
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

struct text_node *
text_node_create(struct wlr_scene_tree *parent, char *text) {
    assert(server.config->font);

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

// function to decode a single utf8 character into a utf32 code point; note: this function can read data from outside
// its buffer. i dont think thats a problem since it cannot write it? todo: add end pointer in any case
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

// wayland protocol specifies that a string must be a valid utf8, so i will trust it that it does
static size_t
utf8_strlen(const char *str) {
    size_t len = 0;
    while(*str) {
        if((*str & 0xC0) != 0x80) {
            len++;
        }
        str++;
    }

    return len;
}

static uint32_t
render_text(struct pixman_buffer *buffer, const char32_t *text, size_t len, pixman_image_t *color) {
    long x = 0;
    for(size_t i = 0; i < len; i++) {
        const struct fcft_glyph *glyph = fcft_rasterize_char_utf32(server.config->font, text[i], FCFT_SUBPIXEL_NONE);
        if(glyph == NULL) continue;

        // add the kerning
        if(i > 0) {
            long kern = 0;
            fcft_kerning(server.config->font, text[i - 1], text[i], &kern, NULL);
            x += kern;
        }

        // composite the image into the buffer
        pixman_image_composite32(PIXMAN_OP_OVER, color, glyph->pix, buffer->image, 0, 0, 0, 0, x + glyph->x,
                server.config->font->ascent - glyph->y, glyph->width, glyph->height);
        // and advance the position for the next one
        x += glyph->advance.x;
    }

    return x;
}

void
text_node_set_text(struct text_node *node, char *text) {
    if(text == NULL) return;

    size_t len = utf8_strlen(text);

    // we approximate the width of the text
    uint32_t width = len * (server.config->font->max_advance.x);
    uint32_t height = server.config->font->max_advance.y;

    if(node->buffer != NULL) {
        pixman_buffer_destroy(node->buffer);
    }
    node->buffer = pixman_buffer_create(width, height);

    wlr_scene_buffer_set_buffer(node->scene_buffer, &node->buffer->base);

    // if the len is 0 then we attach the empty buffer and return
    if(len == 0) return;

    // we first convert the string to utf32
    char32_t utf32[len];
    size_t i = 0, j = 0;
    while(j < len) {
        ssize_t move_forward = convert_utf8_to_utf32(&text[i], &utf32[j]);
        // if its invalid utf8 then we quit
        if(move_forward == -1) return;

        i += move_forward;
        j++;
    }

    pixman_color_t color;
    mwc_color_to_pixman_color(server.config->titlebar_title_color, &color);
    pixman_image_t *foreground_color = pixman_image_create_solid_fill(&color);

    node->width = render_text(node->buffer, utf32, len, foreground_color);
    node->height = height;

    pixman_image_unref(foreground_color);
}
