#include "text_buffer.h"

#include "mwc.h"
#include "config.h"
#include "wlr/util/log.h"

#include <assert.h>
#include <drm_fourcc.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <fcft/fcft.h>
#include <pixman.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/interfaces/wlr_buffer.h>

extern struct mwc_server server;

static void
pixman_buffer_handle_destroy(struct wlr_buffer *wlr_buffer) {
  /* i could not understand when this gets called, so i just clean it up myself */
}

static bool
pixman_buffer_handle_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
                                           uint32_t flags, void **data,
                                           uint32_t *format, size_t *stride) {
  struct pixman_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);

  *data = pixman_image_get_data(buffer->image);
  *stride = pixman_image_get_stride(buffer->image);
  *format = DRM_FORMAT_ARGB8888;

  return true;
}

static void
pixman_buffer_handle_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
  /* this space is intentionally left blank */
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

void
pixman_buffer_fill_solid(struct pixman_buffer *buffer, float color[static 4]) {
  uint32_t stride = pixman_image_get_stride(buffer->image) / sizeof(uint32_t);
  uint32_t *data = pixman_image_get_data(buffer->image);

  uint32_t c = (uint32_t)(color[0] * 255) <<  0 |
               (uint32_t)(color[1] * 255) <<  8 |
               (uint32_t)(color[2] * 255) << 16 |
               (uint32_t)(color[3] * 255) << 24;

  pixman_fill(data, stride, sizeof(uint32_t), 0, 0, buffer->width, buffer->height, c);

}

static uint32_t
render_glyphs_to_pixman_buffer(struct pixman_buffer *buffer, pixman_image_t *color,
                               size_t count, const struct fcft_glyph *glyphs[static count],
                               long kern[static count]) {
  long x = 0;

  for(size_t i = 0; i < count; i++) {
    const struct fcft_glyph *g = glyphs[i];
    if(g == NULL) continue;

    x += kern[i];

    pixman_image_composite32(PIXMAN_OP_OVER, color, g->pix, buffer->image, 0, 0, 0, 0,
                             x + g->x, server.config->font->ascent - g->y, g->width, g->height);

    x += g->advance.x;
  }

  return x;
}

static uint32_t
render_chars_to_pixman_buffer(const char32_t *text, size_t len, struct pixman_buffer *buffer, pixman_image_t *color) {
  const struct fcft_glyph *glyphs[len];
  long kern[len];

  for(size_t i = 0; i < len; i++) {
    glyphs[i] = fcft_rasterize_char_utf32(server.config->font, text[i], FCFT_SUBPIXEL_DEFAULT);
    if(glyphs[i] == NULL) continue;

    kern[i] = 0;
    if(i > 0) {
      fcft_kerning(server.config->font, text[i - 1], text[i], &kern[i], NULL);
    }
  }

  return render_glyphs_to_pixman_buffer(buffer, color, len, glyphs, kern);
}

static void
convert_cstring_to_unicode(char *src, char32_t *dest) {
  while(*src != 0) {
    *dest++ = (char32_t)(unsigned char)*src++;
  }

  *dest = U'\0';
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
  if(node->buffer != NULL) {
    pixman_buffer_destroy(node->buffer); 
  }

  free(node);
}

void
text_node_set_text(struct text_node *node, char *text) {
  if(text == NULL) return;

  node->text = text;

  /* we approximate the width of the text */
  size_t len = strlen(text);
  uint32_t width = len * (server.config->font->max_advance.x);
  uint32_t height = server.config->font->max_advance.y;

  /* TODO: save an allocation if the current is bigger than this one */
  if(node->buffer != NULL) {
    pixman_buffer_destroy(node->buffer);
  }
  node->buffer = pixman_buffer_create(width, height);
  pixman_buffer_fill_solid(node->buffer, (float[4]){ 1, 0, 0, 1 });
  wlr_scene_buffer_set_buffer_with_damage(node->scene_buffer, &node->buffer->base, NULL);

  char32_t unicode[len + 1];
  convert_cstring_to_unicode(text, unicode);

  pixman_image_t *foreground_color = pixman_image_create_solid_fill(&server.config->titlebar_title_color);

  node->width = render_chars_to_pixman_buffer(unicode, len, node->buffer, foreground_color);
  node->height = height;

  pixman_image_unref(foreground_color);
}

