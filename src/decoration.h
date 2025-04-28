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

    struct wlr_scene_tree *tree;

    struct wlr_scene_rect *border;
    struct wlr_scene_shadow *shadow;

    struct {
        struct wlr_scene_tree *tree;

        struct wlr_scene_rect *base;
        struct wlr_scene_rect *close_button;
        struct text_node *title;
    } titlebar;

    // current state
    uint32_t width, height;
    bool active;
    bool blur, blur_optimized;
};

// create a new decoration with `parent` as parent scene tree and `types` of decoration
struct decoration *
decoration_create(struct wlr_scene_tree *parent, uint32_t types);

void
decoration_destroy(struct decoration *decoration);

// set decoration types to a bitmask of `decorations_type`
// this function can be called multiple times to update the wanted decorations
void
decoration_set_types(struct decoration *decoration, uint types);

// enable or disable the decoration, controlling if they are drawn or not
void
decoration_set_enabled(struct decoration *decoration, bool enabled);

// set decoration to its active or inactive state
void
decoration_set_active(struct decoration *decoration, bool active);

// configure the decoration to the provided size
void
decoration_configure(struct decoration *decoration, uint32_t width, uint32_t height);

// recreate this decoration; you may call this for it to match the new config
void
decoration_recreate(struct decoration *decoration, uint32_t types);

// set the title if there is one, you can call this function safely even if there isnt a titlebar
void
decoration_titlebar_set_title(struct decoration *decoration, char *title);

// get the content box from the decoration box
struct wlr_box
decoration_get_content_box(struct decoration *decoration, struct wlr_box box);

// get the decoration box from the content box
struct wlr_box
decoration_get_decoration_box(struct decoration *decoration, struct wlr_box box);

enum blur_optimized;

void
decoration_set_blur(struct decoration *decoration, bool blur, bool blur_optimized);

bool
decoration_is_enabled(struct decoration *decoration);

bool
decoration_has_border(struct decoration *decoration);

bool
decoration_has_shadow(struct decoration *decoration);

bool
decoration_has_titlebar(struct decoration *decoration);

void
server_handle_request_xdg_decoration(struct wl_listener *listener, void *data);
