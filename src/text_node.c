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
#include <wlr/types/wlr_output.h>

#include "font.h"
#include "helpers.h"

static void
handle_node_destroy(struct wl_listener *listener, void *data) {
    struct text_node *node = wl_container_of(listener, node, node_destroy);

    if(node->buffer != NULL) {
        wlr_buffer_drop(&node->buffer->base);
    }

    wl_list_remove(&node->node_destroy.link);
    wl_list_remove(&node->output_update.link);

    if(node->text != NULL) {
        free(node->text);
    }
    free(node);
}

static void
handle_output_update(struct wl_listener *listener, void *data) {
    struct text_node *node = wl_container_of(listener, node, output_update);

    struct wlr_scene_output *output = node->scene_buffer->primary_output;
    if(output != NULL) {
        text_node_set_scale(node, output->output->scale);
    }
}

struct text_node *
text_node_create(struct wlr_scene_tree *parent, struct font *font, float scale, struct color color, char *text) {
    struct text_node *node = calloc(1, sizeof(*node));
    node->font = font;
    node->color = color;
    node->scale = scale;
    node->current_font = font_get_at_scale(font, scale);

    node->scene_buffer = wlr_scene_buffer_create(parent, NULL);

    node->node_destroy.notify = handle_node_destroy;
    wl_signal_add(&node->scene_buffer->node.events.destroy, &node->node_destroy);

    node->output_update.notify = handle_output_update;
    wl_signal_add(&node->scene_buffer->events.outputs_update, &node->output_update);

    text_node_set_text(node, text);

    return node;
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
        // invalid utf8
        return -1;
    }
}

static int
utf8_strlen(const char *str) {
    int len = 0;
    while(*str) {
        if((*str & 0xC0) != 0x80) {
            len++;
        }
        str++;
    }

    return len;
}

static int
render_text(struct pixman_buffer *buffer, struct fcft_font *font, const char32_t *text, size_t len,
        pixman_image_t *color) {
    long x = 0;
    for(size_t i = 0; i < len; i++) {
        const struct fcft_glyph *glyph = fcft_rasterize_char_utf32(font, text[i], FCFT_SUBPIXEL_NONE);
        if(glyph == NULL)
            continue;

        // add the kerning
        if(i > 0) {
            long kern = 0;
            fcft_kerning(font, text[i - 1], text[i], &kern, NULL);
            x += kern;
        }

        // composite the image into the buffer
        pixman_image_composite32(PIXMAN_OP_OVER, color, glyph->pix, buffer->image, 0, 0, 0, 0, x + glyph->x,
                font->ascent - glyph->y, glyph->width, glyph->height);
        // and advance the position for the next one
        x += glyph->advance.x;
    }

    return x;
}

static void
render_current_text(struct text_node *node) {
    // drop the old buffer if any
    if(node->buffer != NULL) {
        wlr_buffer_drop(&node->buffer->base);
    }

    int len;
    if(node->current_font == NULL || node->text == NULL || (len = utf8_strlen(node->text)) == 0) {
        // we attach the empty buffer and return
        node->buffer = NULL;
        wlr_scene_buffer_set_buffer(node->scene_buffer, NULL);
        return;
    }

    // we approximate the width of the text
    int width = len * (node->current_font->max_advance.x);
    // todo: test with font->height
    int height = node->current_font->height;

    node->buffer = pixman_buffer_create(width, height);
    wlr_scene_buffer_set_buffer(node->scene_buffer, &node->buffer->base);

    // we first convert the string to utf32
    char32_t utf32[len];
    int i = 0, j = 0;
    while(j < len) {
        int move_forward = convert_utf8_to_utf32(&node->text[i], &utf32[j]);
        // if its invalid utf8 then we quit
        if(move_forward == -1)
            return;

        i += move_forward;
        j++;
    }

    pixman_color_t color;
    color_to_pixman_color(node->color, &color);
    pixman_image_t *foreground_color = pixman_image_create_solid_fill(&color);

    node->width = render_text(node->buffer, node->current_font, utf32, len, foreground_color);
    node->height = height;

    pixman_image_unref(foreground_color);
}

void
text_node_set_text(struct text_node *node, char *text) {
    // free the old text
    if(node->text != NULL) {
        free(node->text);
    }

    node->text = text == NULL ? NULL : strdup(text);
    render_current_text(node);
}

void
text_node_set_scale(struct text_node *node, float scale) {
    if(scale == node->scale)
        return;

    node->current_font = font_get_at_scale(node->font, scale);
    // rerender the text for the new scale
    render_current_text(node);
    // why the fuck this works ???
    wlr_scene_buffer_set_dest_size(node->scene_buffer, node->width, node->height / scale);
    // todo: test this when at setup against moving between outputs
    node->width /= scale;
    node->height /= scale;
}
