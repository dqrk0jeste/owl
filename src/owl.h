#pragma once

#include "pointer.h"

#include <wayland-server-protocol.h>
#include <wlr/util/box.h>

#define max(a, b) (a) > (b) ? (a) : (b)
#define min(a, b) (a) < (b) ? (a) : (b)

#define STRING_INITIAL_LENGTH 64

enum owl_direction {
  OWL_UP,
  OWL_RIGHT,
  OWL_DOWN,
  OWL_LEFT,
};

struct owl_server {
	struct wl_display *wl_display;
	struct wl_event_loop *wl_event_loop;
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

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;

  struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
  /* keeps track if there is a layer surface that takes exclusive keyboard focus */
  struct owl_layer_surface *layer_exclusive_keyboard;
  /* what to return focus to after its unmapped */
  struct owl_toplevel *prev_focused;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;

	enum owl_cursor_mode cursor_mode;
  /* this keeps state when the compositor is in the state of moving or
   * resizing toplevels */
	struct owl_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grabbed_toplevel_initial_box;
	uint32_t resize_edges;

  /* keeps state about the client cursor when the server initialized move/resize */
  struct {
    struct wlr_surface *surface;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
  } client_cursor;

  /* active workspace follows mouse */
  struct owl_workspace *active_workspace;
  /* toplevel with keyboard focus */
  struct owl_toplevel *focused_toplevel;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

  struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
  struct wl_listener request_xdg_decoration;

  struct wlr_xdg_output_manager_v1 *xdg_output_manager;
  struct wlr_data_control_manager_v1 *data_control_manager;
  struct wlr_viewporter *viewporter;
  struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;
  struct wlr_screencopy_manager_v1 *screencopy_manager;

  struct owl_config *config;
};

