#pragma once

#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "animations.h"
#include "decoration.h"
#include "mwc.h"
#include "rendering.h"

struct mwc_toplevel {
    struct wl_list link;
    struct wlr_xdg_toplevel *xdg_toplevel;

    struct mwc_workspace *workspace;
    struct decoration *decoration;

    struct wlr_scene_tree *scene_tree;

    bool floating;
    bool fullscreen;
    // if a floating toplevel becomes fullscreen, we keep its previous state here
    struct wlr_box prev_deco_box;

    // set for floating when they should choose their size
    bool should_choose_size;
    // this is set on map so the toplevel is setup for the popin effect animation
    bool needs_popin_adjustment;
    // if this toplevel has titlebar
    bool has_titlebar;
    // toplevel (with decorations) size and position of the toplevel (with decorations) in the layout
    struct wlr_box box, deco_box;

    // cached values for toplevels opacity
    double inactive_opacity, active_opacity;

    struct fx_transform_animation *animation;

    struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel_handle;

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

#define X(t) ((t)->scene_tree->node.x)
#define Y(t) ((t)->scene_tree->node.y)

struct mwc_token {
    struct wlr_xdg_activation_token_v1 *wlr_token;

    struct wl_listener destroy;
};

// look up window rules to find the size of this toplevel
bool
toplevel_get_floating_deco_size(struct mwc_toplevel *toplevel, uint32_t *width, uint32_t *height);

// send the configure of 0, 0 and set things up for patching later using `toplevel_floating_patch_for_own_size()`
void
toplevel_floating_set_own_size(struct mwc_toplevel *toplevel);

// sets the new state for this toplevels including decorations and sends the right configure event
// this should be the only way we reposition and/or resize the clients
void
toplevel_set_state(struct mwc_toplevel *toplevel, struct wlr_box deco_box);

// get the reported geometry; more ergonomic wrapper around the wlroots version of the function
struct wlr_box
toplevel_get_geometry(struct mwc_toplevel *toplevel);

// get currently displayed toplevel box; caused by running animation
struct wlr_box
toplevel_get_current_display_box(struct mwc_toplevel *toplevel);

// get currently displayed size of this toplevel; caused by running animation
void
toplevel_get_current_display_size(struct mwc_toplevel *toplevel, uint32_t *width, uint32_t *height);

// get currently displayed toplevel box with decorations; caused by running animation
struct wlr_box
toplevel_get_current_display_deco_box(struct mwc_toplevel *toplevel);

// get currently displayed size with decorations of this toplevel; caused by running animation
void
toplevel_get_current_display_deco_size(struct mwc_toplevel *toplevel, uint32_t *width, uint32_t *height);

// translates the no decorations box to one with decorations; stripping the decorations
struct wlr_box
deco_box_to_box(struct wlr_box box, struct decoration *decoration);

// translates the toplevel box to one with decorations; adding the decorations
struct wlr_box
box_to_deco_box(struct wlr_box box, struct decoration *decoration);

void
server_handle_new_toplevel(struct wl_listener *listener, void *data);

void
toplevel_start_move(struct mwc_toplevel *toplevel, bool client_driven);

void
toplevel_start_resize(struct mwc_toplevel *toplevel, uint32_t edges, bool client_driven);

void
cursor_jump_focused_toplevel(void);

void
toplevel_set_fullscreen(struct mwc_toplevel *toplevel);

void
toplevel_unset_fullscreen(struct mwc_toplevel *toplevel);

void
unfocus_focused_toplevel(void);

// tries to give the keyboard focus to this toplevel
void
focus_toplevel(struct mwc_toplevel *toplevel);

struct mwc_toplevel *
toplevel_find_closest_floating_on_workspace(struct mwc_toplevel *toplevel, enum mwc_direction direction);

// get the output where the most of this toplevel is drawn on
struct mwc_output *
toplevel_get_primary_output(struct mwc_toplevel *toplevel);

// get the corner closest to the cursor; FIXME: this should take the x, y coords instead
uint32_t
toplevel_get_closest_corner(struct wlr_cursor *cursor, struct mwc_toplevel *toplevel);

// recheck the opacity rules; FIXME: this should be more general and check other window rules
void
toplevel_recheck_opacity_rules(struct mwc_toplevel *toplevel);

void
xdg_activation_handle_new_token(struct wl_listener *listener, void *data);

void
xdg_activation_handle_request(struct wl_listener *listener, void *data);
