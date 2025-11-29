#include <regex.h>
#include <scenefx/types/wlr_scene.h>

#include "layer_surface.h"

#include "config.h"
#include "mwc.h"
#include "popup.h"
#include "output.h"
#include "something.h"
#include "layout.h"
#include "toplevel.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "workspace.h"

#include <math.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_fractional_scale_v1.h>

extern struct mwc_server server;

void
server_handle_new_layer_surface(struct wl_listener *listener, void *data) {
  struct wlr_layer_surface_v1 *wlr_layer_surface = data;

  struct mwc_layer_surface *layer_surface = calloc(1, sizeof(*layer_surface));
  layer_surface->wlr_layer_surface = wlr_layer_surface;
  wlr_layer_surface->data = layer_surface;

  layer_surface->something.type = MWC_LAYER_SURFACE;
  layer_surface->something.layer_surface = layer_surface;
  
  if(layer_surface->wlr_layer_surface->output == NULL) {
    /* we give it currently active output */
    layer_surface->wlr_layer_surface->output = server.active_workspace->output->wlr_output;
  }

  struct mwc_output *output = layer_surface->wlr_layer_surface->output->data;
  wlr_fractional_scale_v1_notify_scale(layer_surface->wlr_layer_surface->surface,
                                       output->wlr_output->scale);
  wlr_surface_set_preferred_buffer_scale(layer_surface->wlr_layer_surface->surface,
                                         ceil(output->wlr_output->scale));

  enum zwlr_layer_shell_v1_layer layer = wlr_layer_surface->pending.layer;

  struct wlr_scene_tree *scene = layer_get_scene(layer);
  layer_surface->scene = wlr_scene_layer_surface_v1_create(scene, wlr_layer_surface);

  if(output->active_workspace->fullscreen_toplevel != NULL &&
     (layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM || layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP)) {
    wlr_scene_node_set_enabled(&layer_surface->scene->tree->node, false);
  }

  struct wl_list *list = layer_get_list(output, layer);
  wl_list_insert(list, &layer_surface->link);

  layer_surface->scene->tree->node.data = &layer_surface->something;

  layer_surface->commit.notify = layer_surface_handle_commit;
  wl_signal_add(&wlr_layer_surface->surface->events.commit, &layer_surface->commit);

  layer_surface->map.notify = layer_surface_handle_map;
  wl_signal_add(&wlr_layer_surface->surface->events.map, &layer_surface->map);

  layer_surface->unmap.notify = layer_surface_handle_unmap;
  wl_signal_add(&wlr_layer_surface->surface->events.unmap, &layer_surface->unmap);

  layer_surface->new_popup.notify = layer_surface_handle_new_popup;
  wl_signal_add(&wlr_layer_surface->events.new_popup, &layer_surface->new_popup);

  layer_surface->destroy.notify = layer_surface_handle_destroy;
  wl_signal_add(&wlr_layer_surface->surface->events.destroy, &layer_surface->destroy);
}

void
layer_surface_handle_commit(struct wl_listener *listener, void *data) {
  struct mwc_layer_surface *layer_surface = wl_container_of(listener, layer_surface, commit);

  if(!layer_surface->wlr_layer_surface->initialized) return;

  struct mwc_output *output = layer_surface->wlr_layer_surface->output->data;

  uint32_t committed = layer_surface->wlr_layer_surface->current.committed;
	enum zwlr_layer_shell_v1_layer layer = layer_surface->wlr_layer_surface->current.layer;
	if(committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
    /* if the layer has been changed we respect it */
		struct wl_list *list = layer_get_list(output, layer);
    wl_list_remove(&layer_surface->link);
    wl_list_insert(list, &layer_surface->link);

		struct wlr_scene_tree *scene = layer_get_scene(layer);
		wlr_scene_node_reparent(&layer_surface->scene->tree->node, scene);
	}

  /* if its the first commit or something has changed we rearange the surfaces */
  if(layer_surface->wlr_layer_surface->initial_commit || committed) {
		layer_surfaces_commit(output);
	}

  if(server.config->blur && layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
    wlr_scene_optimized_blur_mark_dirty(output->scene_output->scene,output->blur,output->wlr_output);
  }
}

void
iter_scene_buffer_apply_blur(struct wlr_scene_buffer *buffer,
                             int sx, int sy, void *data) {
  wlr_scene_buffer_set_backdrop_blur(buffer, data);
  wlr_scene_buffer_set_backdrop_blur_optimized(buffer, data);
  wlr_scene_buffer_set_backdrop_blur_ignore_transparent(buffer, data);
}

void
layer_surface_handle_map(struct wl_listener *listener, void *data) {
  struct mwc_layer_surface *layer_surface = wl_container_of(listener, layer_surface, map);
  struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->wlr_layer_surface;

  struct mwc_output *output = wlr_layer_surface->output->data;

  wlr_scene_node_raise_to_top(&layer_surface->scene->tree->node);
  if(server.config->blur) {
    struct layer_rule_blur *b;
    wl_list_for_each(b, &server.config->layer_rules.blur, link) {
      if(!b->condition.has || regexec(&b->condition.regex, wlr_layer_surface->namespace, 0, NULL, 0) == 0) {
        wlr_scene_node_for_each_buffer(&layer_surface->scene->tree->node, iter_scene_buffer_apply_blur, (void *)1);
      }
    }
  }

  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

  wlr_scene_layer_surface_v1_configure(layer_surface->scene, &output_box, &output->usable_area);

  layout_set_pending_state(output->active_workspace);

  focus_layer_surface(layer_surface);
}

void
layer_surface_handle_unmap(struct wl_listener *listener, void *data) {
  struct mwc_layer_surface *layer_surface = wl_container_of(listener, layer_surface, unmap);

  wl_list_remove(&layer_surface->link);

  struct mwc_output *output = layer_surface->wlr_layer_surface->output->data;

  if(output == NULL) {
    if(layer_surface == server.focused_layer_surface) {
      server.focused_layer_surface = NULL;
      server.exclusive = false;
    }
    wlr_layer_surface_v1_destroy(layer_surface->wlr_layer_surface);
    return;
  }

  if(layer_surface == server.focused_layer_surface) {
    /* focusing next will set it */
    server.exclusive = false;
    
    bool focused = false;
    struct mwc_layer_surface *l;
    wl_list_for_each(l, &output->layers.overlay, link) {
      if(l->wlr_layer_surface->current.keyboard_interactive) {
        focus_layer_surface(l);
        focused = true;
      }
    }
    wl_list_for_each(l, &output->layers.top, link) {
      if(l->wlr_layer_surface->current.keyboard_interactive) {
        focus_layer_surface(l);
        focused = true;
      }
    }

    if(!focused) {
      /* dont focus things that are not on the screen */
      if(server.prev_focused != NULL
         && server.prev_focused->workspace == server.active_workspace) {
        focus_toplevel(server.prev_focused);
      } else if(!wl_list_empty(&server.active_workspace->masters)) {
        struct mwc_toplevel *first = wl_container_of(server.active_workspace->masters.next,
                                                     first, link);
        focus_toplevel(first);
      } else if(!wl_list_empty(&server.active_workspace->floating_toplevels)) {
        struct mwc_toplevel *first = wl_container_of(server.active_workspace->floating_toplevels.next,
                                                     first, link);
        focus_toplevel(first);
      }
    }
  }

  layer_surfaces_commit(output);
}

void
layer_surface_handle_destroy(struct wl_listener *listener, void *data) {
  struct mwc_layer_surface *layer_surface = wl_container_of(listener, layer_surface, destroy);

  wl_list_remove(&layer_surface->map.link);
  wl_list_remove(&layer_surface->unmap.link);
  wl_list_remove(&layer_surface->destroy.link);

  free(layer_surface);
}

void
layer_surface_handle_new_popup(struct wl_listener *listener, void *data) {
  struct mwc_layer_surface *layer_surface = wl_container_of(listener,
                                                            layer_surface, new_popup);
  struct wlr_xdg_popup *xdg_popup = data;

  /* see server_handle_new_xdg_popup() */
  struct mwc_popup *popup = xdg_popup->base->data;

  struct wlr_scene_tree *parent_tree = layer_surface->scene->tree;
  popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

  popup->something.type = MWC_POPUP;
  popup->something.popup = popup;

  popup->scene_tree->node.data = &popup->something;
  popup->xdg_popup->base->data = popup->scene_tree;
}

void
focus_layer_surface(struct mwc_layer_surface *layer_surface) {
  if(server.lock != NULL) return;

  enum zwlr_layer_surface_v1_keyboard_interactivity keyboard_interactive =
    layer_surface->wlr_layer_surface->current.keyboard_interactive;

  if(keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) return;

  if(keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
     && server.exclusive) return;

  /* unfocus the focused toplevel */
  if(server.focused_toplevel != NULL) {
    server.prev_focused = server.focused_toplevel;
    unfocus_focused_toplevel();
  }

  server.focused_layer_surface = layer_surface;
  server.exclusive = keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat);
  if(keyboard != NULL) {
    wlr_seat_keyboard_notify_enter(server.seat, layer_surface->wlr_layer_surface->surface,
                                   keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
  }
}

void
layer_surfaces_commit_layer(struct mwc_output *output,
                            enum zwlr_layer_shell_v1_layer layer, bool exclusive) {
	struct wl_list *list = layer_get_list(output, layer);

  struct wlr_box full_area;
  wlr_output_layout_get_box(server.output_layout, output->wlr_output, &full_area);

  struct mwc_layer_surface *l;
	wl_list_for_each_reverse(l, list, link) {
		if((l->wlr_layer_surface->current.exclusive_zone > 0) != exclusive) continue;

		wlr_scene_layer_surface_v1_configure(l->scene, &full_area, &output->usable_area);
	}
}

void
layer_surfaces_commit(struct mwc_output *output) {
  struct wlr_box full_area;
  wlr_output_layout_get_box(server.output_layout, output->wlr_output, &full_area);

  output->usable_area = full_area;

  /* first commit all the exclusive ones */
  for(size_t i = 0; i < 4; i++) {
    layer_surfaces_commit_layer(output, i, true);
  }

  /* then all the others */
  for(size_t i = 0; i < 4; i++) {
    layer_surfaces_commit_layer(output, i, false);
  }

  layout_set_pending_state(output->active_workspace);
}

struct wlr_scene_tree *
layer_get_scene(enum zwlr_layer_shell_v1_layer layer) {
  switch(layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
      return server.background_tree;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
      return server.bottom_tree;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
      return server.top_tree;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
      return server.overlay_tree;
  }
}

struct wl_list *
layer_get_list(struct mwc_output *output, enum zwlr_layer_shell_v1_layer layer) {
  switch(layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
      return &output->layers.background;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
      return &output->layers.bottom;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
      return &output->layers.top;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
      return &output->layers.overlay;
  }
}

/* we dont touch background as we want it seen behind the fullscreened toplevel */
void
layers_under_fullscreen_set_enabled(struct mwc_output *output, bool enable) {
  struct mwc_layer_surface *l;
  wl_list_for_each(l, &output->layers.bottom, link) {
    wlr_scene_node_set_enabled(&l->scene->tree->node, enable);
  }
  wl_list_for_each(l, &output->layers.top, link) {
    wlr_scene_node_set_enabled(&l->scene->tree->node, enable);
  }
}
