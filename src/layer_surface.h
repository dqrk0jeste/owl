#pragma once

#include <wlr/types/wlr_layer_shell_v1.h>

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

void
layer_surface_handle_commit(struct wl_listener *listener, void *data);

void
layer_surface_handle_map(struct wl_listener *listener, void *data);

void
layer_surface_handle_unmap(struct wl_listener *listener, void *data);

void
layer_surface_handle_destroy(struct wl_listener *listener, void *data);

void
layer_surface_handle_new_popup(struct wl_listener *listener, void *data);

void
server_handle_new_layer_surface(struct wl_listener *listener, void *data);

void
focus_layer_surface(struct owl_layer_surface *layer_surface);
