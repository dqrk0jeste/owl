#pragma once

#include <scenefx/types/wlr_scene.h>

#include "rendering.h"
#include "mwc.h"
#include "config.h"
#include "something.h"
#include "animations.h"

#include <stdint.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>

struct mwc_toplevel {
    struct wl_list link;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct mwc_workspace *workspace;

    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_rect *border;
    struct wlr_scene_shadow *shadow;

    struct mwc_something something;

    bool floating;
    bool fullscreen;
    // if a floating toplevel becomes fullscreen, we keep its previous state here
    struct wlr_box prev_geometry;

    bool resizing;

    uint32_t configure_serial;
    bool dirty;

    // toplevel size and position of the toplevel in the layout
    struct wlr_box toplevel_current, toplevel_pending;
    // container size and position of the toplevel in the layout
    struct wlr_box container_current, container_pending;

    // cached values for toplevels opacity
    double inactive_opacity, active_opacity;

    struct fx_translate_animation *animation;
    struct wl_event_source *animation_timer;
    // start time in miliseconds
    uint64_t animation_start;
    // this flag should be set when applying new state we want animated to
    bool should_animate_next;

    struct {
        bool has;
        struct wlr_scene_tree *tree;
        struct wlr_scene_rect *base;
        struct wlr_scene_rect *close_button;
        struct text_node *title;
        struct mwc_something base_something;
        struct mwc_something close_button_something;
    } titlebar;

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

// looks up window rules and returns true if found, with the size in `*width` and `*height`, else return false
bool
toplevel_get_floating_container_size(struct mwc_toplevel *toplevel, uint32_t *width, uint32_t *height);

// strips the decoration from the sizes contained in `*width` and `*height`
void
toplevel_strip_decorations_of_size(uint32_t *width, uint32_t *height, bool has_border, bool has_titlebar);

// adds the decoration to the sizes contained in `*width` and `*height`
void
toplevel_add_decorations_to_size(uint32_t *width, uint32_t *height, bool has_border, bool has_titlebar);

// translates the container box to toplevel one; stripping the decorations
struct wlr_box
toplevel_container_box_to_toplevel_box(struct wlr_box box, bool has_border, bool has_titlebar);

// translates the toplevel box to container one; adding the decorations
struct wlr_box
toplevel_toplevel_box_to_container_box(struct wlr_box box, bool has_border, bool has_titlebar);

// send the configure of 0, 0 and set things up for patching later using `toplevel_floating_patch_for_own_size()`
void
toplevel_floating_set_own_size(struct mwc_toplevel *toplevel);

// sets the new state for this toplevels container and sends the right configure event
void
toplevel_set_pending_state(struct mwc_toplevel *toplevel,
        int32_t x, int32_t y, uint32_t width, uint32_t height);

// commit to the pending state for this toplevel
void
toplevel_commit(struct mwc_toplevel *toplevel);

// patches the floating toplevel when it was send 0, 0 for its size, so we respect its chosen size and center it
void
toplevel_floating_patch_for_own_size(struct mwc_toplevel *toplevel);

// get the reported geometry; more ergonomic wrapper around the wlroots version of the function
struct wlr_box
toplevel_get_geometry(struct mwc_toplevel *toplevel);

// get currently displayed size of this toplevel; caused by running animation
void
toplevel_get_current_display_toplevel_size(struct mwc_toplevel *toplevel,
                                           uint32_t *width, uint32_t *height);

// get currently displayed toplevel box; caused by running animation
struct wlr_box
toplevel_get_current_display_toplevel_box(struct mwc_toplevel *toplevel);

void
server_handle_new_toplevel(struct wl_listener *listener, void *data);

void
toplevel_handle_commit(struct wl_listener *listener, void *data);

void
toplevel_handle_initial_commit(struct mwc_toplevel *toplevel);

void
toplevel_handle_map(struct wl_listener *listener, void *data);

void
toplevel_handle_unmap(struct wl_listener *listener, void *data);

void
toplevel_handle_destroy(struct wl_listener *listener, void *data);

void
toplevel_start_move(struct mwc_toplevel *toplevel);

void
toplevel_start_resize(struct mwc_toplevel *toplevel, uint32_t edges);

void
toplevel_handle_request_move(struct wl_listener *listener, void *data);

void
toplevel_handle_request_resize(struct wl_listener *listener, void *data);

void
toplevel_handle_request_maximize(struct wl_listener *listener, void *data);

void
toplevel_handle_request_fullscreen(struct wl_listener *listener, void *data);

void
toplevel_handle_set_app_id(struct wl_listener *listener, void *data);

void
toplevel_handle_set_title(struct wl_listener *listener, void *data);

bool
toplevel_matches_window_rule(struct mwc_toplevel *toplevel,
        struct window_rule_regex *condition);

// looks up window rules
bool
toplevel_should_float(struct mwc_toplevel *toplevel);

// looks up config and window rules
bool
toplevel_should_draw_titlebar(struct mwc_toplevel *toplevel);

void
cursor_jump_focused_toplevel(void);

void
toplevel_set_fullscreen(struct mwc_toplevel *toplevel);

void
toplevel_unset_fullscreen(struct mwc_toplevel *toplevel);

void
toplevel_move(void);

void
toplevel_resize(void);

// inserts the toplevel into layout at these coords
void
toplevel_tiled_insert_into_layout(struct mwc_toplevel *toplevel, uint32_t x, uint32_t y);

void
unfocus_focused_toplevel(void);

// tries to give the focus to this toplevel; handles only keyboard focus
void
focus_toplevel(struct mwc_toplevel *toplevel);

struct mwc_toplevel *
toplevel_find_closest_floating_on_workspace(struct mwc_toplevel *toplevel,
        enum mwc_direction direction);

// get the output where the most of this toplevel is drawn on
struct mwc_output *
toplevel_get_primary_output(struct mwc_toplevel *toplevel);

// get the corner closest to the cursor; FIXME: this should take the x, y coords instead
uint32_t
toplevel_get_closest_corner(struct wlr_cursor *cursor,
        struct mwc_toplevel *toplevel);

// recheck the opacity rules; FIXME: this should be more general and check other window rules
void
toplevel_recheck_opacity_rules(struct mwc_toplevel *toplevel);

void
xdg_activation_handle_new_token(struct wl_listener *listener, void *data);

void
xdg_activation_handle_request(struct wl_listener *listener, void *data);

