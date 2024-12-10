#include "layer_surface.h"

#include "owl.h"
#include "output.h"
#include "something.h"
#include "layout.h"

#include <stdlib.h>
#include <wlr/types/wlr_scene.h>

extern struct owl_server server;

void
layer_surface_handle_commit(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, commit);

  if(!layer_surface->wlr_layer_surface->initialized) {
    return;
  }

  if(layer_surface->wlr_layer_surface->initial_commit) {
    struct owl_output *output = layer_surface->wlr_layer_surface->output->data;

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    struct wlr_box temp = output->usable_area;
    wlr_scene_layer_surface_v1_configure(layer_surface->scene, &output_box, &output->usable_area);
  }
}

void
layer_surface_handle_map(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, map);
  struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->wlr_layer_surface;

  enum zwlr_layer_shell_v1_layer layer = wlr_layer_surface->pending.layer;
  struct owl_output *output = wlr_layer_surface->output->data;

  wlr_scene_node_raise_to_top(&layer_surface->scene->tree->node);

  switch(layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
      wl_list_insert(&output->layers.background, &layer_surface->link);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
      wl_list_insert(&output->layers.bottom, &layer_surface->link);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
      wl_list_insert(&output->layers.top, &layer_surface->link);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
      wl_list_insert(&output->layers.overlay, &layer_surface->link);
      break;
  }

  int x, y;
  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

  struct wlr_box temp = output->usable_area;
  wlr_scene_layer_surface_v1_configure(layer_surface->scene, &output_box, &output->usable_area);

  if(temp.width != output->usable_area.width || temp.height != output->usable_area.height) {
    layout_set_pending_state(output->active_workspace);
  }

  focus_layer_surface(layer_surface);
}

void
layer_surface_handle_unmap(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, unmap);

  struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->wlr_layer_surface;
  struct wlr_layer_surface_v1_state *state = &layer_surface->wlr_layer_surface->current;
  struct owl_output *output = layer_surface->wlr_layer_surface->output->data;

  if(layer_surface == server.layer_exclusive_keyboard) {
    server.layer_exclusive_keyboard = NULL;

    if(server.prev_focused != NULL) {
      focus_toplevel(server.prev_focused);
    }
  }

  if(layer_surface->wlr_layer_surface->current.exclusive_zone > 0) {
    switch (state->anchor) {
      case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP:
      case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
        // Anchor top
        output->usable_area.y -= state->exclusive_zone + state->margin.top;
        output->usable_area.height += state->exclusive_zone + state->margin.top;
        break;
      case ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM:
      case (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
        // Anchor bottom
        output->usable_area.height += state->exclusive_zone + state->margin.bottom;
        break;
      case ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT:
      case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT):
        // Anchor left
        output->usable_area.x -= state->exclusive_zone + state->margin.left;
        output->usable_area.width += state->exclusive_zone + state->margin.left;
        break;
      case ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
      case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
        // Anchor right
        output->usable_area.width += state->exclusive_zone + state->margin.right;
        break;
    }

    layout_set_pending_state(output->active_workspace);
  }

  wl_list_remove(&layer_surface->link);
}

void
layer_surface_handle_destroy(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, destroy);

  wl_list_remove(&layer_surface->map.link);
  wl_list_remove(&layer_surface->unmap.link);
  wl_list_remove(&layer_surface->destroy.link);

  free(layer_surface);
}

void
layer_surface_handle_new_popup(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, new_popup);
  struct wlr_xdg_popup *xdg_popup = data;

  /* see server_handle_new_xdg_popup */
  struct owl_popup *popup = xdg_popup->base->data;

  struct wlr_scene_tree *parent_tree = layer_surface->scene->tree;
  popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

  /* TODO: this is going to be used to get the layer surface from the popup */
  struct owl_something *something = calloc(1, sizeof(*something));
  something->type = OWL_POPUP;
  something->popup = popup;
  popup->scene_tree->node.data = something;

  popup->xdg_popup->base->data = popup->scene_tree;
}

void
server_handle_new_layer_surface(struct wl_listener *listener, void *data) {
  struct wlr_layer_surface_v1 *wlr_layer_surface = data;

  struct owl_layer_surface *layer_surface = calloc(1, sizeof(*layer_surface));
  layer_surface->wlr_layer_surface = wlr_layer_surface;
  layer_surface->wlr_layer_surface->data = layer_surface;

  enum zwlr_layer_shell_v1_layer layer = wlr_layer_surface->pending.layer;
  switch(layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
      layer_surface->scene =
        wlr_scene_layer_surface_v1_create(server.background_tree, wlr_layer_surface);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
      layer_surface->scene =
        wlr_scene_layer_surface_v1_create(server.bottom_tree, wlr_layer_surface);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
      layer_surface->scene =
        wlr_scene_layer_surface_v1_create(server.top_tree, wlr_layer_surface);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
      layer_surface->scene =
        wlr_scene_layer_surface_v1_create(server.overlay_tree, wlr_layer_surface);
      break;
  }

  struct owl_something *something = calloc(1, sizeof(*something));
  something->type = OWL_LAYER_SURFACE;
  something->layer_surface = layer_surface;

  layer_surface->scene->tree->node.data = something;

  if(layer_surface->wlr_layer_surface->output == NULL) {
    layer_surface->wlr_layer_surface->output = server.active_workspace->output->wlr_output;
  }

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
focus_layer_surface(struct owl_layer_surface *layer_surface) {
  enum zwlr_layer_surface_v1_keyboard_interactivity keyboard_interactive =
    layer_surface->wlr_layer_surface->current.keyboard_interactive;

  switch(keyboard_interactive) {
    case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE: {
      return;
    }
    case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE: {
      server.prev_focused = server.focused_toplevel;
      unfocus_focused_toplevel();
      server.layer_exclusive_keyboard = layer_surface;
      struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat);
      if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server.seat, layer_surface->wlr_layer_surface->surface,
                                       keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
      }
      return;
    }
    case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND: {
      if(server.layer_exclusive_keyboard != NULL) return;
      server.prev_focused = server.focused_toplevel;
      unfocus_focused_toplevel();
      struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat);
      if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server.seat,
                                       layer_surface->wlr_layer_surface->surface,
                                       keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
      }
      return;
    }
  }
}

