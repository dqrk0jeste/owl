#ifndef OWL_H
#define OWL_H

#include <assert.h>
#include <getopt.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <regex.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
#include <libinput.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>

#include "wlr/util/box.h"

#define max(a, b) (a) > (b) ? (a) : (b)
#define min(a, b) (a) < (b) ? (a) : (b)

enum owl_cursor_mode {
	OWL_CURSOR_PASSTHROUGH,
	OWL_CURSOR_MOVE,
	OWL_CURSOR_RESIZE,
};

enum owl_direction {
  UP,
  RIGHT,
  DOWN,
  LEFT,
};

struct window_rule_float {
  regex_t app_id_regex;
  regex_t title_regex;
};

struct window_rule_size {
  regex_t app_id_regex;
  regex_t title_regex;
  bool relative_width;
  uint32_t width;
  bool relative_height;
  uint32_t height;
};

struct output_config {
  char *name;
  struct wl_list link;
  uint32_t width;
  uint32_t height;
  uint32_t refresh_rate;
  uint32_t x;
  uint32_t y;
};

typedef void (*keybind_action_func_t)(void *);

struct keybind {
  uint32_t modifiers;
  uint32_t sym;
  keybind_action_func_t action;
  bool active;
  keybind_action_func_t stop;
  void *args;
  struct wl_list link;
};

struct workspace_config {
  uint32_t index;
  char *output;
  struct wl_list link;
};

struct owl_config {
  struct wl_list outputs;
  struct wl_list keybinds;
  struct wl_list workspaces;
  struct {
    struct window_rule_float floating[64];
    size_t floating_count;
    struct window_rule_size size[64];
    size_t size_count;
  } window_rules;
  uint32_t keyboard_rate;
  uint32_t keyboard_delay;
  char cursor_theme[256];
  uint32_t cursor_size;
  uint32_t min_toplevel_size;
  float inactive_border_color[4];
  float active_border_color[4];
  uint32_t border_width;
  uint32_t outer_gaps;
  uint32_t inner_gaps;
  uint32_t master_count;
  double master_ratio;
  bool natural_scroll;
  bool tap_to_click;
  char *run[64];
  size_t run_count;
};

struct owl_workspace {
  struct wl_list link;
  struct owl_output *output;
  uint32_t index;
  struct wl_list masters;
  struct wl_list slaves;
  struct wl_list floating_toplevels;
  struct owl_toplevel *fullscreen_toplevel;
};

struct owl_output {
	struct wl_list link;
	struct wlr_output *wlr_output;
  struct wl_list workspaces;
  struct wlr_box usable_area;
  struct {
    struct wl_list background;
    struct wl_list bottom;
    struct wl_list top;
    struct wl_list overlay;
  } layers;
  struct owl_workspace *active_workspace;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

enum owl_type {
  OWL_TOPLEVEL,
  OWL_POPUP,
  OWL_LAYER_SURFACE,
};

struct owl_something {
  enum owl_type type;
  union {
    struct owl_toplevel *toplevel;
    struct owl_popup *popup;
    struct owl_layer_surface *layer_surface;
  };
};

struct owl_layer_surface {
  struct wl_list link;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  struct wlr_scene_layer_surface_v1 *scene;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener commit;
  struct wl_listener new_popup;
  struct wl_listener destroy;
};

struct owl_toplevel {
	struct wl_list link;
	struct wlr_xdg_toplevel *xdg_toplevel;
  struct owl_workspace *workspace;
	struct wlr_scene_tree *scene_tree;
  struct wlr_scene_rect *borders[4];

  bool mapped;
  bool floating;
  bool fullscreen;
  /* if a floating toplevel becomes fullscreen, we keep its previous state here */
  struct wlr_box prev_geometry;

  /* these are going to be used in the next output frame to draw the thing */
  bool requested_size_change;
  bool responded_to_size_change;
  uint32_t pending_x;
  uint32_t pending_y;
  uint32_t pending_width;
  uint32_t pending_height;

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

struct owl_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct owl_keyboard {
	struct wl_list link;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct owl_server {
	struct wl_display *wl_display;
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

#endif /* ifndef OWL_H */
