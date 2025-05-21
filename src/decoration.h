#pragma once

#include <fcft/fcft.h>
#include <scenefx/types/fx/corner_location.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

enum blur {
    BLUR_NONE = 0,
    BLUR_NORMAL,
    BLUR_OPTIMIZED,
};

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

enum titlebar_title_position {
    TITLEBAR_TITLE_POSITION_LEFT = 0,
    TITLEBAR_TITLE_POSITION_CENTER,
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
    int width, height;
    int min_width, min_height;  // minimum width should be checked when resizing a toplevel
    bool active;
    enum blur blur;
    int corner_radius, corner_location;
    double opacity;

    char *title;
};

// initialize the decoration for the `content_tree`
void
decoration_init(struct decoration *decoration, struct wlr_scene_tree *content_tree);

// recreates the decorations; this is intended to be called when reloading the configuration
void
decoration_recreate(struct decoration *decoration);

// releases all the allocated resources
void
decoration_destroy(struct decoration *decoration);

// configure the decoration to the provided size
void
decoration_configure(struct decoration *decoration, int width, int height);

// set decoration types to a bitmask of `decorations_type`. this function can be called multiple times to update the
// wanted decorations
void
decoration_set_types(struct decoration *decoration, uint32_t types);

void
decoration_set_active(struct decoration *decoration, bool active);

void
decoration_set_title(struct decoration *decoration, char *title);

void
decoration_set_blur(struct decoration *decoration, enum blur blur);

void
decoration_set_opacity(struct decoration *decoration, double opacity);

void
decoration_set_corner_radius(struct decoration *decoration, int corner_radius, enum corner_location corner_location);

// remove the decorations from size in `*width` and `*height`
void
decoration_get_content_size(struct decoration *decoration, int *width, int *height);

// add the decorations to size in `*width` and `*height`
void
decoration_get_decoration_size(struct decoration *decoration, int *width, int *height);

bool
decoration_has_border(struct decoration *decoration);

bool
decoration_has_shadow(struct decoration *decoration);

bool
decoration_has_titlebar(struct decoration *decoration);
