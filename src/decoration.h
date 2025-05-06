#pragma once

#include <fcft/fcft.h>
#include <scenefx/types/fx/corner_location.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

enum decorations_type {
    DECORATION_BORDER = 1,
    DECORATION_SHADOW = 2,
    DECORATION_TITLEBAR = 4,
};

enum titlebar_close_button_shape {
    TITLEBAR_CLOSE_BUTTON_SHAPE_CIRCLE = 0,
    TITLEBAR_CLOSE_BUTTON_SHAPE_SQUARE,
};

enum titlebar_close_button_position {
    TITLEBAR_CLOSE_BUTTON_POSITION_RIGHT = 0,
    TITLEBAR_CLOSE_BUTTON_POSITION_LEFT,
};

struct decoration {
    // bitmask of `decorations_types`
    uint32_t types;

    struct wlr_scene_tree *content_tree;
    struct wlr_scene_tree *tree;

    struct wlr_scene_rect *border;
    struct wlr_scene_shadow *shadow;

    struct {
        struct wlr_scene_tree *tree;

        struct wlr_scene_rect *base;
        struct wlr_scene_rect *close_button;
        struct text_node *title;
    } titlebar;

    // state
    uint32_t width, height, min_width, min_height;
    bool active;
    bool blur, blur_optimized;
    char *title;
};

// initialize the decoration for the `content_tree`
void
decoration_init(struct decoration *decoration, struct wlr_scene_tree *content_tree);

// destroys all the decorations, but keeps the current state; this is intended to be called when reloading the
// configuration
void
decoration_destroy_all(struct decoration *decoration);

// releases all the allocated resources
void
decoration_destroy(struct decoration *decoration);

// set decoration types to a bitmask of `decorations_type`. this function can be called multiple times to update the
// wanted decorations
void
decoration_set_types(struct decoration *decoration, uint32_t types);

bool
decoration_is_enabled(struct decoration *decoration);

// enable or disable the decoration, controlling if they are drawn or not
void
decoration_set_enabled(struct decoration *decoration, bool enabled);

// set decoration to its active or inactive state
void
decoration_set_active(struct decoration *decoration, bool active);

// configure the decoration to the provided size
void
decoration_configure(struct decoration *decoration, uint32_t width, uint32_t height);

// set the title if there is one, you can call this function safely even if there isnt a titlebar
void
decoration_titlebar_set_title(struct decoration *decoration, char *title);

void
decoration_set_blur(struct decoration *decoration, bool blur, bool blur_optimized);

// remove the decorations from size in `*width` and `*height`
void
decoration_get_content_size(struct decoration *decoration, uint32_t *width, uint32_t *height);

// add the decorations to size in `*width` and `*height`
void
decoration_get_decoration_size(struct decoration *decoration, uint32_t *width, uint32_t *height);

bool
decoration_has_border(struct decoration *decoration);

bool
decoration_has_shadow(struct decoration *decoration);

bool
decoration_has_titlebar(struct decoration *decoration);
