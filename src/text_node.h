#include <fcft/fcft.h>
#include <stdint.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include "helpers.h"
#include "pixman_buffer.h"

struct text_node {
    struct pixman_buffer *buffer;
    struct wlr_scene_buffer *scene_buffer;
    struct font *font;
    struct color color;
    float scale;
    // there used to happen a really weird bug when changing the scale. updating the scale would `squish` the text a
    // bit, and that may immediatelly change the primary output the previous one, which would then `grow` the text,
    // setting the new scale again, and so on, getting into a recursive loop, and ending in a stack overflow. this flag
    // makes sure that we only update the scale if it isnt currently being updated
    bool updating_scale;

    char *text;  // utf8 encoded text

    struct fcft_font *current_font;
    // logical size of the node
    int width, height;
    // clipped width
    int clip;

    // this listens for when the buffer node is destroyed, so it can free the other resources
    struct wl_listener node_destroy;
    // so we can update the font scale when needed
    struct wl_listener output_update;
};

// create a new text node. this node should be destroyed by destroying its underlying scene node
struct text_node *
text_node_create(struct wlr_scene_tree *parent, struct font *font, float scale, struct color color, char *text);

// update the text
void
text_node_set_text(struct text_node *node, char *text);

void
text_node_set_clip(struct text_node *node, int width);
