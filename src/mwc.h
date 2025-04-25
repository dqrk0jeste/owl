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

enum direction {
    DIRECTION_UP = 0,
    DIRECTION_RIGHT,
    DIRECTION_DOWN,
    DIRECTION_LEFT,
};

enum server_mode {
    SERVER_MODE_NORMAL = 0,
    SERVER_MODE_DRAGGING,
    SERVER_MODE_CAN_GIVE_FOCUS = SERVER_MODE_DRAGGING,
    SERVER_MODE_MOVING,
    SERVER_MODE_RESIZING,
    SERVER_MODE_RESIZING_MASTER_RATIO,
    SERVER_MODE_LOCKED,
};

struct server {
    struct wl_display *wl_display;
    struct wl_event_loop *wl_event_loop;
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

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_toplevel;
    struct wl_listener new_popup;

    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;

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

    struct wlr_scene_tree *drag_icon_tree;
    struct wl_listener request_drag;
    struct wl_listener request_start_drag;
    struct wl_listener request_destroy_drag;

    struct wl_list pointers;

    struct wl_list keyboards;
    struct keyboard *last_used_keyboard;

    // todo: handle resize of master ratio. do so only when there are slaves
    // this keeps state when the compositor is in the state of moving or resizing toplevels
    double grab_x, grab_y;
    union {
        // for moving/resizing toplevels
        struct {
            struct toplevel *grabbed_toplevel;
            struct wlr_box grabbed_toplevel_initial_box;
            uint32_t resize_edges;
            bool move_resize_by_keybind;
        };
        // for resizing master ratio
        double initial_master_ratio;
    };

    // active workspace follows pointer
    struct workspace *active_workspace;

    // current mode that server is on
    enum server_mode mode;

    // toplevel with keyboard focus
    struct toplevel *focused_toplevel;
    // keeps track if there is a layer surface that takes keyboard focus
    struct layer_surface *focused_layer_surface;
    bool exclusive;
    // last focused toplevel before layer surface was given focus
    struct toplevel *prev_focused;

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
    struct lock *lock;

    struct wlr_pointer_constraints_v1 *pointer_contrains_manager;
    struct wl_listener new_contraint;
    struct pointer_constraint *current_constraint;

    struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
    struct wl_listener relative_pointer_manager_destroy;

    struct wlr_xdg_activation_v1 *xdg_activation;
    struct wl_listener xdg_activation_request;
    struct wl_listener xdg_activation_new_token;

    struct config *config;

    int *ipc_clients;
    bool ipc_running;

    // todo: find a way to remove this
    bool running;
};
