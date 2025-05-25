#pragma once

#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "animations.h"
#include "decoration.h"
#include "foreign_toplevel.h"
#include "helpers.h"

enum toplevel_mode {
    TOPLEVEL_MODE_NONE = 0,
    TOPLEVEL_MODE_FLOATING = 1 << 0,
    TOPLEVEL_MODE_MASTER = 1 << 1,
    TOPLEVEL_MODE_SLAVE = 1 << 2,
    TOPLEVEL_MODE_FULLSCREEN = 1 << 3,
};

struct toplevel {
    struct wl_list link;
    struct wlr_xdg_toplevel *xdg_toplevel;

    // scene tree with decorations
    struct wlr_scene_tree *scene_tree;
    // subsurface tree for this toplevel
    struct wlr_scene_tree *content_tree;

    struct workspace *workspace;
    enum toplevel_mode mode;
    bool is_fake_fullscreen;

    struct decoration decoration;
    struct wlr_xdg_toplevel_decoration_v1 *xdg_decoration;  // may be null

    // if a floating toplevel becomes fullscreen, we keep its previous state
    enum toplevel_mode prev_mode;
    union {
        // when it was tiled we keep its index in the layout. this index should be used as a hint to where to place the
        // toplevel after exiting fullscreen
        int prev_index;
        // if it was floating than we keep its previous position in the layout
        struct wlr_box prev_deco_box;
    };

    // set for floating when they should choose their size
    bool should_choose_size;
    // this is set on map so the toplevel is setup for the popin effect animation
    bool needs_popin_adjustment;
    // toplevel size and position in the layout
    struct wlr_box deco_box;

    // derived from rules
    bool client_side_decorations;
    double opacity;
    enum blur blur;
    int corner_radius;
    enum corner_location corner_location;
    int default_width, default_height;
    bool width_is_relative, height_is_relative;

    struct fx_transform_animation *animation;

    struct foreign_toplevel_handle *foreign_toplevel_handle;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener set_app_id;
    struct wl_listener set_title;
};

bool
toplevel_is_tiled(struct toplevel *toplevel);

// send the configure of 0, 0 and set things up for patching later using `toplevel_handle_own_size()`
void
toplevel_floating_set_own_size(struct toplevel *toplevel);

// sets the new state for this toplevel including decorations and sends the right configure event
// this should be the only way we reposition and/or resize the clients
void
toplevel_set_state(struct toplevel *toplevel, struct wlr_box deco_box);

// get the reported geometry; more ergonomic wrapper around the wlroots version of the function
struct wlr_box
toplevel_get_geometry(struct toplevel *toplevel);

// get currently displayed toplevel content box; caused by running animation
void
toplevel_get_current_display_content_size(struct toplevel *toplevel, int *width, int *height);

// get currently displayed toplevel box with decorations; caused by running animation
struct wlr_box
toplevel_get_current_display_deco_box(struct toplevel *toplevel);

void
handle_new_toplevel(struct wl_listener *listener, void *data);

void
toplevel_start_move(struct toplevel *toplevel, bool by_keybind);

void
toplevel_start_resize(struct toplevel *toplevel, uint32_t edges, bool by_keybind);

void
toplevel_set_fullscreen(struct toplevel *toplevel);

void
toplevel_unset_fullscreen(struct toplevel *toplevel);

void
unfocus_focused_toplevel(void);

// tries to give the keyboard focus to this toplevel
void
focus_toplevel(struct toplevel *toplevel, bool jump_cursor);

struct toplevel *
toplevel_find_closest_floating_on_workspace(struct toplevel *toplevel, enum direction direction);
// raise this toplevel and its parents/children to the top of its scene graph
void
toplevel_raise_to_top(struct toplevel *toplevel);
