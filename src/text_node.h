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
    double scale;

    char *text;  // utf8 encoded text

    struct fcft_font *current_font;
    int width, height;

    // this listens for when the buffer node is destroyed, so it can free the other resources
    struct wl_listener node_destroy;
    // so we can update the font scale when needed
    struct wl_listener output_update;
};

// create a new text node. this node should be destroyed by destroying its underlying scene node
struct text_node *
text_node_create(struct wlr_scene_tree *parent, struct font *font, double scale, struct color color, char *text);

// update the text
void
text_node_set_text(struct text_node *node, char *text);

void
text_node_set_scale(struct text_node *node, double scale);
