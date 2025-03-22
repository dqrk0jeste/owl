#include <fcft/fcft.h>
#include <stdint.h>
#include <pixman.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

struct pixman_buffer {
	struct wlr_buffer base;
  pixman_image_t *image;

  uint32_t width, height;
};

struct pixman_buffer *
pixman_buffer_create(uint32_t width, uint32_t height);

void
pixman_buffer_destroy(struct pixman_buffer *buffer);

void
pixman_buffer_clip_to_size(struct pixman_buffer *buffer, uint32_t width, uint32_t height);

struct text_node {
  struct pixman_buffer *buffer;
  struct wlr_scene_buffer *scene_buffer;

  uint32_t width, height;

  char *text;
};

struct text_node *
text_node_create(struct wlr_scene_tree *parent, char *text);

void
text_node_destroy(struct text_node *node);

void
text_node_set_text(struct text_node *node, char *text);

