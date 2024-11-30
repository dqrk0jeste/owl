#pragma once

#include <wlr/types/wlr_xdg_shell.h>

#include "rendering.h"

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

