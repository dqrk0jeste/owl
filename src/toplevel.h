#pragma once

#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "animations.h"
#include "decoration.h"
#include "foreign_toplevel.h"
#include "helpers.h"
#include "mwc.h"
#include "rendering.h"

enum toplevel_mode {
    TOPLEVEL_MODE_NONE = 0,
    TOPLEVEL_MODE_FLOATING,
    TOPLEVEL_MODE_MASTER,
    TOPLEVEL_MODE_SLAVE,
    TOPLEVEL_MODE_FULLSCREEN,
};

struct toplevel {
    struct wl_list link;

    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;

    struct workspace *workspace;
    enum toplevel_mode mode;

    uint32_t wanted_decoration_types;
    struct decoration *decoration;

    // if a floating toplevel becomes fullscreen, we keep its previous state
    enum toplevel_mode prev_mode;
    union {
        // when it was tiled we keep its index in the layout. this index should be used as a hint to where to place the
        // toplevel after exiting fullscreen
        uint32_t prev_index;
        // if it was floating than we keep its previous position in the layout
        struct wlr_box prev_deco_box;
    };

    // set for floating when they should choose their size
    bool should_choose_size;
    // this is set on map so the toplevel is setup for the popin effect animation
    bool needs_popin_adjustment;
    // toplevel size and position in the layout
    struct wlr_box deco_box;

    // cached values for toplevels opacity and blur, since they are needed every frame
    double inactive_opacity, active_opacity, has_blur;

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

// looks up window rules and returns true if found, with the size in `*width` and `*height`, else return false
bool
toplevel_get_floating_deco_size(struct toplevel *toplevel, uint32_t *width, uint32_t *height);

// send the configure of 0, 0 and set things up for patching later using `toplevel_floating_patch_for_own_size()`
void
toplevel_floating_set_own_size(struct toplevel *toplevel);

// sets the new state for this toplevels including decorations and sends the right configure event
// this should be the only way we reposition and/or resize the clients
void
toplevel_set_state(struct toplevel *toplevel, struct wlr_box deco_box);

// get the reported geometry; more ergonomic wrapper around the wlroots version of the function
struct wlr_box
toplevel_get_geometry(struct toplevel *toplevel);

// get currently displayed toplevel content box; caused by running animation
struct wlr_box
toplevel_get_current_display_content_box(struct toplevel *toplevel);

// get currently displayed toplevel box with decorations; caused by running animation
struct wlr_box
toplevel_get_current_display_deco_box(struct toplevel *toplevel);

void
server_handle_new_toplevel(struct wl_listener *listener, void *data);

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

// get the output where the most of this toplevel is drawn on
struct output *
toplevel_get_primary_output(struct toplevel *toplevel);

// get the corner closest to the cursor; FIXME: this should take the x, y coords instead
uint32_t
toplevel_get_closest_corner(struct wlr_cursor *cursor, struct toplevel *toplevel);

// shorthand to check if the toplevel should have optimized blur or not
bool
toplevel_should_have_optimized_blur(struct toplevel *toplevel);

// raise this toplevel and its parents/children to the top of its scene graph
// note: toplevel must be floating
void
toplevel_raise_to_top(struct toplevel *toplevel);

void
xdg_activation_handle_request(struct wl_listener *listener, void *data);

void
server_handle_request_xdg_decoration(struct wl_listener *listener, void *data);
