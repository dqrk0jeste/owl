#pragma once

#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/util/box.h>

#include "config.h"
#include "constraint.h"
#include "cursor.h"
#include "cursor_shape.h"
#include "gamma_control.h"
#include "keyboard.h"
#include "layer_shell.h"
#include "relative_pointer.h"
#include "seat.h"
#include "session_lock.h"
#include "xdg_shell.h"

#define MWC_VERSION 0.2.0

enum server_mode {
    SERVER_MODE_NORMAL = 0,
    SERVER_MODE_DRAGGING,
    SERVER_MODE_CAN_GIVE_FOCUS = SERVER_MODE_DRAGGING,
    SERVER_MODE_MOVING,
    SERVER_MODE_RESIZING,
    SERVER_MODE_RESIZING_MASTER_RATIO,
    SERVER_MODE_LOCKED,
    SERVER_MODE_SHUTTING,
};

struct server {
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wlr_session *session;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_scene_tree *background_tree;
    struct wlr_scene_tree *bottom_tree;
    struct wlr_scene_tree *tiled_tree;
    struct wlr_scene_tree *floating_tree;
    struct wlr_scene_tree *top_tree;
    struct wlr_scene_tree *fullscreen_tree;
    struct wlr_scene_tree *overlay_tree;
    struct wlr_scene_tree *session_lock_tree;
    struct wlr_scene_tree *drag_icon_tree;

    struct wl_list outputs;
    struct wlr_output_layout *output_layout;
    struct wl_listener new_output;

    struct seat seat;

    struct xdg_shell xdg_shell;
    struct layer_shell layer_shell;

    struct cursor cursor;
    struct cursor_shape_manager cursor_shape_manager;

    struct wl_list pointers;

    struct wl_list keyboards;
    struct keyboard *last_used_keyboard;

    // current mode that server is on
    enum server_mode mode;

    // this keeps state when the compositor is in the state of moving or resizing toplevels
    double grab_x, grab_y;
    // for moving/resizing toplevels
    struct wlr_scene_tree *grabbed_tree;
    struct toplevel *grabbed_toplevel;
    struct wlr_box grabbed_toplevel_initial_box;
    uint32_t resize_edges;
    bool move_resize_by_keybind;
    // for resizing master ratio
    double initial_master_ratio;

    // active workspace follows pointer
    struct workspace *active_workspace;

    // toplevel with keyboard focus
    struct toplevel *focused_toplevel;
    // keeps track if there is a layer surface that takes keyboard focus
    struct layer_surface *focused_layer_surface;
    bool exclusive;
    // last focused toplevel before layer surface was given focus
    struct toplevel *prev_focused;

    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;

    struct gamma_manager gamma_manager;

    struct lock_manager lock_manager;
    struct relative_pointer_manager relative_pointer_manager;
    struct constraint_manager constraint_manager;

    char *config_path;
    struct config *config;
    struct {
        struct wl_event_source *source;
        int fd, wd;
    } config_watcher;

    struct fx_animation_curve *animation_curve;
    struct font *title_font;

    struct {
        struct wl_event_source *source;
        int fd;
        int *watching_workspace, *watching_toplevel, *watching_layer;  // array
    } ipc;
};
