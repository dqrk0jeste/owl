#pragma once

#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>

#include "rendering.h"
#include "owl.h"
#include "pointer.h"
#include "config.h"

struct owl_animation;

struct owl_toplevel {
	struct wl_list link;
	struct wlr_xdg_toplevel *xdg_toplevel;
  struct owl_workspace *workspace;
	struct wlr_scene_tree *scene_tree;
  struct wlr_scene_rect *borders[4];

  bool mapped;
  bool rendered;

  bool floating;
  bool fullscreen;
  /* if a floating toplevel becomes fullscreen, we keep its previous state here */
  struct wlr_box prev_geometry;

  bool resizing;

  uint32_t configure_serial;
  bool dirty;

  /* state to be applied to this toplevel; values of 0 mean that the client should
   * choose its size and need to be handled seperately */
  struct wlr_box pending;

  struct owl_animation animation;

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

/* some macros for commonly accessed fields of a toplevel */
#define X(t) (t)->scene_tree->node.x
#define Y(t) (t)->scene_tree->node.y
#define WIDTH(t) (t)->xdg_toplevel->base->geometry.width
#define HEIGHT(t) (t)->xdg_toplevel->base->geometry.height

void
toplevel_start_move_resize(struct owl_toplevel *toplevel,
                                  enum owl_cursor_mode mode, uint32_t edges);

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

void
server_handle_new_toplevel(struct wl_listener *listener, void *data);

void
toplevel_handle_map(struct wl_listener *listener, void *data);

void
toplevel_handle_unmap(struct wl_listener *listener, void *data);

void
toplevel_handle_commit(struct wl_listener *listener, void *data);

void
toplevel_handle_destroy(struct wl_listener *listener, void *data);

bool
toplevel_position_changed(struct owl_toplevel *toplevel);

bool
toplevel_size_changed(struct owl_toplevel *toplevel);

bool
toplevel_matches_window_rule(struct owl_toplevel *toplevel,
                             struct window_rule_regex *condition);

void
toplevel_floating_size(struct owl_toplevel *toplevel, uint32_t *width, uint32_t *height);

bool
toplevel_should_float(struct owl_toplevel *toplevel);

void
cursor_jump_focused_toplevel(void);

void
toplevel_center_floating(struct owl_toplevel *toplevel);

void
toplevel_set_pending_state(struct owl_toplevel *toplevel, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height);

void
toplevel_commit(struct owl_toplevel *toplevel);

void
toplevel_set_fullscreen(struct owl_toplevel *toplevel);

void
toplevel_unset_fullscreen(struct owl_toplevel *toplevel);

void
toplevel_move(void);

void
toplevel_resize(void);

void
unfocus_focused_toplevel(void);

void
focus_toplevel(struct owl_toplevel *toplevel);

struct owl_toplevel *
toplevel_find_closest_floating_on_workspace(struct owl_toplevel *toplevel,
                                            enum owl_direction direction);

struct owl_output *
toplevel_get_primary_output(struct owl_toplevel *toplevel);

void
toplevel_clip_to_size(struct owl_toplevel *toplevel,
                      uint32_t width, uint32_t height);

void
toplevel_clip_to_fit(struct owl_toplevel *toplevel);

void
toplevel_unclip_size(struct owl_toplevel *toplevel);

uint32_t
toplevel_get_closest_corner(struct wlr_cursor *cursor,
                            struct owl_toplevel *toplevel);

struct owl_toplevel *
get_pointer_focused_toplevel(void);

void
toplevel_set_initial_state(struct owl_toplevel *toplevel, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height);
