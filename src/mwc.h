#pragma once

#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/util/box.h>

#include "keyboard.h"
#include "pointer.h"
#include "session_lock.h"

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define STRING_INITIAL_LENGTH 64

enum mwc_direction {
    MWC_UP,
    MWC_RIGHT,
    MWC_DOWN,
    MWC_LEFT,
};

struct mwc_server {
    struct wl_display *wl_display;
    struct wl_event_loop *wl_event_loop;
    struct wlr_session *session;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_scene_tree *floating_tree;
    struct wlr_scene_tree *tiled_tree;
    struct wlr_scene_tree *background_tree;
    struct wlr_scene_tree *bottom_tree;
    struct wlr_scene_tree *top_tree;
    struct wlr_scene_tree *fullscreen_tree;
    struct wlr_scene_tree *overlay_tree;
    struct wlr_scene_tree *session_lock_tree;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;

    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;

    struct wl_list pointers;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct wlr_cursor_shape_manager_v1 *cursor_shape_manager;
    struct wl_listener request_cursor_shape;
    struct wl_listener cursor_shape_manager_destroy;

    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;

    bool drag_active;
    struct wlr_scene_tree *drag_icon_tree;
    struct wl_listener request_drag;
    struct wl_listener request_start_drag;
    struct wl_listener request_destroy_drag;

    struct wl_list keyboards;
    struct mwc_keyboard *last_used_keyboard;

    enum mwc_cursor_mode cursor_mode;
    // this keeps state when the compositor is in the state of moving or resizing toplevels
    struct mwc_toplevel *grabbed_toplevel;
    double grab_x, grab_y;
    struct wlr_box grabbed_toplevel_initial_box;
    uint32_t resize_edges;
    bool client_driven_move_resize;

    // active workspace follows pointer
    struct mwc_workspace *active_workspace;
    // toplevel with keyboard focus
    struct mwc_toplevel *focused_toplevel;
    // keeps track if there is a layer surface that takes keyboard focus
    struct mwc_layer_surface *focused_layer_surface;
    bool exclusive;
    // last focused toplevel before layer surface was given focus
    struct mwc_toplevel *prev_focused;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;

    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
    struct wl_listener request_xdg_decoration;

    struct wlr_server_decoration_manager *kde_decoration_manager;

    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;

    struct wlr_gamma_control_manager_v1 *gamma_control_manager;
    struct wl_listener set_gamma;

    struct wlr_session_lock_manager_v1 *session_lock_manager;
    struct wl_listener new_lock;
    struct wl_listener lock_manager_destroy;
    struct mwc_lock *lock;

    struct wlr_pointer_constraints_v1 *pointer_contrains_manager;
    struct wl_listener new_contraint;
    struct mwc_pointer_constraint *current_constraint;

    struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
    struct wl_listener relative_pointer_manager_destroy;

    struct wlr_xdg_activation_v1 *xdg_activation;
    struct wl_listener xdg_activation_request;
    struct wl_listener xdg_activation_new_token;

    struct mwc_config *config;

    int *ipc_clients;
    bool ipc_running;

    bool running;
};
