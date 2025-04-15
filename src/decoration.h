#pragma once

#include <fcft/fcft.h>
#include <scenefx/types/fx/corner_location.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "helpers.h"

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

struct decoration_config {
    uint32_t border_width;
    uint32_t border_radius;
    enum corner_location border_radius_location;
    struct {
        struct mwc_color active, inactive;
    } border_color;

    uint32_t shadows_size;
    struct {
        int32_t x, y;
    } shadows_position;
    struct mwc_color shadows_color;
    double shadows_blur;

    uint32_t titlebar_height;
    struct {
        struct mwc_color active, inactive;
    } titlebar_color;

    bool titlebar_include_close_button;
    uint32_t titlebar_close_button_size;
    struct {
        uint32_t left, right;
    } titlebar_close_button_padding;
    enum titlebar_close_button_shape titlebar_close_button_shape;
    enum titlebar_close_button_position titlebar_close_button_position;
    struct {
        struct mwc_color active, inactive;
    } titlebar_close_button_color;

    bool titlebar_include_title;
    bool titlebar_center_title;
    struct {
        uint32_t left, right;
    } titlebar_title_padding;
    struct mwc_color titlebar_title_color;

    // will be generated from the name specified by `titlebar_title_font`, may be NULL
    struct fcft_font *font;
};

struct decoration_manager {
    bool inited;

    struct decoration_config *config;
    struct wl_list decorations;
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

    uint32_t width, height;
    struct wlr_box content_box;

    struct wl_list link;
};

// initializes the decoration manager with the provided config
// this can be called multiple times to update the config. all the decorations created up to that moment will be
// automatically updated to this new config
void
decoration_manager_init(struct decoration_config *config);

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

struct wlr_box
decoration_get_content_box(struct decoration *decoration, struct wlr_box box);

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
