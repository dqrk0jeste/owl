#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
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

#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "wlr/util/box.h"
#include "xdg-shell-protocol.h"

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

struct owl_config {
  struct wl_list monitors;
  struct wl_list keybinds;
  uint32_t keyboard_rate;
  uint32_t keyboard_delay;
  char* cursor_theme;
  uint32_t min_toplevel_size;
  uint32_t workspaces_per_monitor;
  float inactive_border_color[4];
  float active_border_color[4];
  uint32_t border_width;
  uint32_t outer_gaps;
  uint32_t inner_gaps;
  double master_ratio;
  bool natural_scroll;
  bool tap_to_click;
};

struct monitor_config {
  char* name;
  struct wl_list link;
  uint32_t width;
  uint32_t height;
  uint32_t refresh_rate;
  uint32_t x;
  uint32_t y;
};

struct owl_server;
typedef void (*keybind_action_func_t)(struct owl_server *, void *);

struct keybind {
  uint32_t modifiers;
  uint32_t sym;
  keybind_action_func_t action;
  bool active;
  keybind_action_func_t stop;
  void *args;
  struct wl_list link;
};

struct owl_workspace;

struct owl_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	struct wlr_scene_tree *toplevel_tree;
	struct wlr_scene_tree *background_tree;
	struct wlr_scene_tree *bottom_tree;
	struct wlr_scene_tree *top_tree;
	struct wlr_scene_tree *overlay_tree;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;

  struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
  struct owl_layer_surface *layer_exlusive_keyboard;

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

	struct owl_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

  struct {
    struct wlr_surface *surface;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
  } client_cursor;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
  struct wlr_xdg_output_manager_v1 *xdg_output_manager;

  struct wlr_data_control_manager_v1 *data_control_manager;

  struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
  struct wl_listener request_xdg_decoration;

  struct wlr_viewporter *viewporter;

  struct owl_config *config;
  /* active workspace follows mouse */
  struct owl_workspace *active_workspace;
};

struct owl_workspace {
  struct wl_list link;
  struct owl_output *output;
  uint32_t index;
  struct owl_toplevel *master;
  struct wl_list slaves;
  struct wl_list floating_toplevels;
};

struct owl_output {
	struct wl_list link;
	struct owl_server *server;
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
  struct owl_server *server;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  struct wlr_scene_layer_surface_v1 *scene_tree;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener commit;
  struct wl_listener new_popup;
  struct wl_listener destroy;
};

struct owl_toplevel {
	struct wl_list link;
	struct owl_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
  struct owl_workspace *workspace;
  struct wlr_scene_rect *borders[4];
  uint32_t width;
  uint32_t height;
  bool floating;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
};

struct owl_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct owl_keyboard {
	struct wl_list link;
	struct owl_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};


static int box_area(struct wlr_box *box) {
  return box->width * box->height;
}

static bool toplevel_dimensions_changed(struct owl_toplevel *toplevel, 
    uint32_t new_width, uint32_t new_height) {
  return toplevel->width != new_width || toplevel->height != new_height;
}

static bool toplevel_position_changed(struct owl_toplevel *toplevel, 
    uint32_t new_x, uint32_t new_y) {
  return toplevel->scene_tree->node.x != new_x
    || toplevel->scene_tree->node.y != new_y;
}

struct owl_output *get_output_relative(struct owl_server *server,
    struct owl_output *output, enum owl_direction direction) {
  struct wlr_box original_output_box;
  wlr_output_layout_get_box(server->output_layout,
    output->wlr_output, &original_output_box);

  uint32_t original_output_midpoint_x =
    original_output_box.x + original_output_box.width / 2;
  uint32_t original_output_midpoint_y =
    original_output_box.y + original_output_box.height / 2;

  struct owl_output *o;
  wl_list_for_each(o, &server->outputs, link) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server->output_layout, o->wlr_output, &output_box);

    if(direction == LEFT &&
        original_output_box.x == output_box.x + output_box.width
        && original_output_midpoint_y > output_box.y
        && original_output_midpoint_y < output_box.y + output_box.height) {
      return o;
    } else if(direction == RIGHT
        && original_output_box.x + original_output_box.width == output_box.x
        && original_output_midpoint_y > output_box.y
        && original_output_midpoint_y < output_box.y + output_box.height) {
      return o;
    } else if(direction == UP
        && original_output_box.y == output_box.y + output_box.height
        && original_output_midpoint_x > output_box.x
        && original_output_midpoint_x < output_box.x + output_box.width) {
      return o;
    } else if(direction == DOWN
        && original_output_box.y + original_output_box.height == output_box.y
        && original_output_midpoint_x > output_box.x
        && original_output_midpoint_x < output_box.x + output_box.width) {
      return o;
    }
  }

  return NULL;
}

static struct owl_toplevel *toplevel_parent_of_surface(struct wlr_surface *wlr_surface) {
  struct wlr_surface *root_wlr_surface = wlr_surface_get_root_surface(wlr_surface);
  struct wlr_xdg_surface *xdg_surface =
    wlr_xdg_surface_try_from_wlr_surface(root_wlr_surface);
  if(xdg_surface == NULL) {
    return NULL;
  }
  
	struct wlr_scene_tree *tree = xdg_surface->data;
  struct owl_something *something = tree->node.data;
  while(something == NULL || something->type == OWL_POPUP) {
    tree = tree->node.parent;
    something = tree->node.data;
  }

  return something->toplevel;
}

static struct owl_toplevel *get_keyboard_focused_toplevel(struct owl_server *server) {
  struct wlr_surface *focused_surface = server->seat->keyboard_state.focused_surface;
  if(focused_surface == NULL) {
    return NULL;
  }
  return toplevel_parent_of_surface(focused_surface);
}

static struct owl_toplevel *get_pointer_focused_toplevel(struct owl_server *server) {
  struct wlr_surface *focused_surface = server->seat->pointer_state.focused_surface;
  if(focused_surface == NULL) {
    return NULL;
  }
  return toplevel_parent_of_surface(focused_surface);
}

static void cursor_jump_focused_toplevel(struct owl_server *server) {
  struct owl_toplevel *toplevel = get_keyboard_focused_toplevel(server);
  if(toplevel == NULL) return;

  struct wlr_box geo_box = toplevel->xdg_toplevel->base->geometry;
  wlr_cursor_warp(server->cursor, NULL,
    toplevel->scene_tree->node.x + geo_box.x + geo_box.width / 2.0,
    toplevel->scene_tree->node.y + geo_box.y + geo_box.height / 2.0);
}

static void cursor_jump_output(struct owl_output *output) {
  struct owl_server *server = output->server;

  struct wlr_box output_box;
  wlr_output_layout_get_box(server->output_layout,
    output->wlr_output, &output_box);

  wlr_cursor_warp(server->cursor, NULL,
    output_box.x + output_box.width / 2.0,
    output_box.y + output_box.height / 2.0);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	struct owl_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static void unfocus_focused_toplevel(struct owl_server *server) {
	struct wlr_seat *seat = server->seat;

  struct owl_toplevel *toplevel = get_keyboard_focused_toplevel(server);
	if(toplevel == NULL) return;

  /* deactivate the previously focused surface */
  wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);

  /* paint its borders to inactive color */
  wlr_scene_rect_set_color(toplevel->borders[0], server->config->inactive_border_color);
  wlr_scene_rect_set_color(toplevel->borders[1], server->config->inactive_border_color);
  wlr_scene_rect_set_color(toplevel->borders[2], server->config->inactive_border_color);
  wlr_scene_rect_set_color(toplevel->borders[3], server->config->inactive_border_color);

  /* clear all focus on the keyboard, focusing new should set new toplevel focus */
  wlr_seat_keyboard_clear_focus(server->seat);
}

static void focus_toplevel(struct owl_toplevel *toplevel) {
	struct owl_server *server = toplevel->server;

  if(server->layer_exlusive_keyboard != NULL) return;

  struct owl_toplevel *prev_toplevel = get_keyboard_focused_toplevel(server);
	if(prev_toplevel == toplevel) return;

  unfocus_focused_toplevel(server);

  if(toplevel->floating) {
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);
  }

	/* activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

  /* paint borders to active color */
  wlr_scene_rect_set_color(toplevel->borders[0], server->config->active_border_color);
  wlr_scene_rect_set_color(toplevel->borders[1], server->config->active_border_color);
  wlr_scene_rect_set_color(toplevel->borders[2], server->config->active_border_color);
  wlr_scene_rect_set_color(toplevel->borders[3], server->config->active_border_color);

	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	struct wlr_seat *seat = server->seat;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, toplevel->xdg_toplevel->base->surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

static bool focus_layer_surface(struct owl_layer_surface *layer_surface) {
  struct owl_server *server = layer_surface->server;

  enum zwlr_layer_surface_v1_keyboard_interactivity keyboard_interactive =
    layer_surface->wlr_layer_surface->current.keyboard_interactive;

  switch (keyboard_interactive) {
    case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE:
      return false;
    case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE: {
      server->layer_exlusive_keyboard = layer_surface;
      struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
      if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server->seat, layer_surface->wlr_layer_surface->surface,
          keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
      }
      return true;
    }
    case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND: {
      if(server->layer_exlusive_keyboard != NULL) return false;
      struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
      if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server->seat, layer_surface->wlr_layer_surface->surface,
          keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
      }
      return true;
    }
  }
}

static bool handle_keybinds(struct owl_server *server, uint32_t modifiers,
    xkb_keysym_t sym, enum wl_keyboard_key_state state) {
  struct keybind *k;
  wl_list_for_each(k, &server->config->keybinds, link) {
    if(k->active && k->stop && sym == k->sym && state == WL_KEYBOARD_KEY_STATE_RELEASED) {
      k->active = false;
      k->stop(server, k->args);
      return true;
    }

    if(modifiers == k->modifiers && sym == k->sym && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      k->active = true;
      k->action(server, k->args);
      return true;
    }
  }

	return false;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	/* This event is raised when a key is pressed or released. */
	struct owl_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct owl_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;

	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
	  keyboard->wlr_keyboard->xkb_state, keycode, &syms);
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	bool handled = false;
	for (int i = 0; i < nsyms; i++) {
		handled = handle_keybinds(server, modifiers, syms[i], event->state);
	}

	if (!handled) {
		/* Otherwise, we pass it along to the client. */
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
  /*
	 * This event is raised by the keyboard base wlr_input_device to signal
	 * the destruction of the wlr_keyboard. It will no longer receive events
	 * and should be destroyed.
   */
	struct owl_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct owl_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct owl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	/* We need to prepare an XKB keymap and assign it to the keyboard. This
	 * assumes the defaults (e.g. layout = "us"). */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

  uint32_t rate = server->config->keyboard_rate;
  uint32_t delay = server->config->keyboard_delay;
	wlr_keyboard_set_repeat_info(wlr_keyboard, rate, delay);

	/* Here we set up listeners for keyboard events. */
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct owl_server *server,
    struct wlr_input_device *device) {
  /* enable natural scrolling and tap to click*/
  if(wlr_input_device_is_libinput(device)) {
    struct libinput_device *libinput_device = wlr_libinput_get_device_handle(device);

    if(libinput_device_config_scroll_has_natural_scroll(libinput_device)
      && server->config->natural_scroll) {
      libinput_device_config_scroll_set_natural_scroll_enabled(libinput_device, true);
    }
    if(libinput_device_config_tap_get_finger_count(libinput_device)
      && server->config->tap_to_click) {
      libinput_device_config_tap_set_enabled(libinput_device, true);
    }
  }

	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct owl_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

  switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
      server_new_keyboard(server, device);
      break;
    case WLR_INPUT_DEVICE_POINTER:
      server_new_pointer(server, device);
      break;
    default:
      /* owl doesnt support touch devices, drawing tablets etc */
      break;
  }

	/* we need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	/* This event is raised by the seat when a client provides a cursor image */
	struct owl_server *server = wl_container_of( listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
    server->client_cursor.surface = event->surface;
    server->client_cursor.hotspot_x = event->hotspot_x;
    server->client_cursor.hotspot_y = event->hotspot_y;
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in owl we always honor
	 */
	struct owl_server *server = wl_container_of(
			listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void toplevel_create_or_update_borders(struct owl_toplevel *toplevel,
    uint32_t width, uint32_t height) {
  struct owl_server *server = toplevel->server;
  uint32_t border_width = server->config->border_width;

  if(toplevel->borders[0] == NULL) {
    toplevel->borders[0] = wlr_scene_rect_create(toplevel->scene_tree,
      width + 2 * border_width, border_width,
      server->config->active_border_color);
    wlr_scene_node_set_position(&toplevel->borders[0]->node,
      -border_width, -border_width);

    toplevel->borders[1] = wlr_scene_rect_create(toplevel->scene_tree,
      border_width, height,
      server->config->active_border_color);
    wlr_scene_node_set_position(&toplevel->borders[1]->node,
      width, 0);

    toplevel->borders[2] = wlr_scene_rect_create(toplevel->scene_tree,
      width + 2 * border_width, border_width,
      server->config->active_border_color);
    wlr_scene_node_set_position(&toplevel->borders[2]->node,
      -border_width, height);

    toplevel->borders[3] = wlr_scene_rect_create(toplevel->scene_tree,
      border_width, height,
      server->config->active_border_color);
    wlr_scene_node_set_position(&toplevel->borders[3]->node,
      -border_width, 0);
    return;
  }

  wlr_scene_node_set_position(&toplevel->borders[1]->node, width, 0);
  wlr_scene_node_set_position(&toplevel->borders[2]->node, -border_width, height);

  wlr_scene_rect_set_size(toplevel->borders[0], width + 2 * border_width, border_width);
  wlr_scene_rect_set_size(toplevel->borders[1], border_width, height);
  wlr_scene_rect_set_size(toplevel->borders[2], width + 2 * border_width, border_width);
  wlr_scene_rect_set_size(toplevel->borders[3], border_width, height);
}

static struct owl_something *owl_something_at(
		struct owl_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	/* This returns the topmost node in the scene at the given layout coords */
	struct wlr_scene_node *node = wlr_scene_node_at(
		&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	*surface = scene_surface->surface;

  struct wlr_scene_tree *tree = node->parent;
  struct owl_something *something = tree->node.data;
  while(something == NULL || something->type == OWL_POPUP) {
    tree = tree->node.parent;
    something = tree->node.data;
  }

  return something;
}

static void reset_cursor_mode(struct owl_server *server) {
	/* Reset the cursor mode to passthrough. */
	server->cursor_mode = OWL_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;

  if(server->client_cursor.surface != NULL) {
		wlr_cursor_set_surface(server->cursor, server->client_cursor.surface,
			server->client_cursor.hotspot_x, server->client_cursor.hotspot_y);
  } else {
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
  }
}

static void process_cursor_move(struct owl_server *server, uint32_t time) {
	/* Move the grabbed toplevel to the new position. */
	struct owl_toplevel *toplevel = server->grabbed_toplevel;
  struct wlr_box geo_box = toplevel->xdg_toplevel->base->geometry;

	int new_x = server->grab_geobox.x + (server->cursor->x - server->grab_x);
	int new_y = server->grab_geobox.y + (server->cursor->y - server->grab_y);

	wlr_scene_node_set_position(&toplevel->scene_tree->node,
    new_x - geo_box.x, new_y - geo_box.y);
}

static void process_cursor_resize(struct owl_server *server, uint32_t time) {
	/*
	 * TODO: wait for the client to prepare a buffer at the new size, then commit
   * any movement that was prepared.
	 */
	struct owl_toplevel *toplevel = server->grabbed_toplevel;

	int start_x = server->grab_geobox.x;
	int start_y = server->grab_geobox.y;
	int start_width = server->grab_geobox.width;
  int start_height = server->grab_geobox.height;

	int new_x = server->grab_geobox.x;
	int new_y = server->grab_geobox.y;
	int new_width = server->grab_geobox.width;
  int new_height = server->grab_geobox.height;

  int min_width = max(toplevel->xdg_toplevel->current.min_width,
    server->config->min_toplevel_size);
  int min_height = max(toplevel->xdg_toplevel->current.min_height,
    server->config->min_toplevel_size);

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_y = start_y + (server->cursor->y - server->grab_y);
    new_height = start_height - (server->cursor->y - server->grab_y);
		if (new_height <= min_height) {
      new_y = start_y + start_height - min_height;
      new_height = min_height;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_y = start_y;
    new_height = start_height + (server->cursor->y - server->grab_y);
		if (new_height <= min_height) {
      new_height = min_height;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_x = start_x + (server->cursor->x - server->grab_x);
    new_width = start_width - (server->cursor->x - server->grab_x);
		if (new_width <= min_width) {
      new_x = start_x + start_width - min_width;
      new_width = min_width;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_x = start_x;
    new_width = start_width + (server->cursor->x - server->grab_x);
		if (new_width <= min_width) {
      new_width = min_width;
		}
	}

	struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		new_x - geo_box->x, new_y - geo_box->y);
  //TODO:
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
  toplevel_create_or_update_borders(toplevel, new_width, new_height);
}

static void process_cursor_motion(struct owl_server *server, uint32_t time) {
  /* get the output that the cursor is on currently */
  struct wlr_output *wlr_output = wlr_output_layout_output_at(
    server->output_layout, server->cursor->x, server->cursor->y);
  struct owl_output *output = wlr_output->data;

  /* set global active workspace */
  if(output->active_workspace != server->active_workspace) {
    server->active_workspace = output->active_workspace;
  }

	if(server->cursor_mode == OWL_CURSOR_MOVE) {
		process_cursor_move(server, time);
		return;
	} else if (server->cursor_mode == OWL_CURSOR_RESIZE) {
		process_cursor_resize(server, time);
		return;
	}

	/* find something under the pointer and send the event along. */
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct owl_something *something = owl_something_at(server,
		server->cursor->x, server->cursor->y, &surface, &sx, &sy);

	if(something == NULL) {
		/* If there's no toplevel under the cursor, set the cursor image to a
		 * default. This is what makes the cursor image appear when you move it
		 * around the screen, not over any toplevels. */
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");

		/* Clear pointer focus so future button events and such are not sent to
		 * the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(seat);
    return;
	}

  if(something->type == OWL_TOPLEVEL) {
    focus_toplevel(something->toplevel);
  } else {
    focus_layer_surface(something->layer_surface);
  }

  wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
  wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct owl_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(
		struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	struct owl_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
		event->y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a button
	 * event. */
	struct owl_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server->seat,
		event->time_msec, event->button, event->state);

  /* TODOFIX */
	if(event->state == WL_POINTER_BUTTON_STATE_RELEASED
    && server->cursor_mode != OWL_CURSOR_PASSTHROUGH) {
    uint32_t grabbed_toplevel_x =
      server->grabbed_toplevel->scene_tree->node.x +
      server->grabbed_toplevel->xdg_toplevel->base->geometry.x;
    uint32_t grabbed_toplevel_y =
      server->grabbed_toplevel->scene_tree->node.y +
      server->grabbed_toplevel->xdg_toplevel->base->geometry.y;

    struct wlr_output *wlr_output = wlr_output_layout_output_at(server->output_layout,
      grabbed_toplevel_x, grabbed_toplevel_y);
    struct owl_output *output = wlr_output->data;

    if(output != server->grabbed_toplevel->workspace->output) {
      server->grabbed_toplevel->workspace->output = output;
      wl_list_remove(&server->grabbed_toplevel->link);
      wl_list_insert(&output->active_workspace->floating_toplevels,
        &server->grabbed_toplevel->link);
    }

		reset_cursor_mode(server);
	}
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct owl_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(server->seat,
		event->time_msec, event->orientation, event->delta,
		event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	struct owl_server *server =
		wl_container_of(listener, server, cursor_frame);

	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(server->seat);
}

static void output_frame(struct wl_listener *listener, void *data) {
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	struct owl_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->scene;

	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		scene, output->wlr_output);

	/* Render the scene if needed and commit the output */
	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

/* this is not really good, but i dont care */
static void output_request_state(struct wl_listener *listener, void *data) {
	/* This function is called when the backend requests a new state for
	 * the output. For example, Wayland and X11 backends request a new mode
	 * when the output window is resized. */
	struct owl_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
  
  struct wlr_box output_box;
  wlr_output_layout_get_box(output->server->output_layout,
    output->wlr_output, &output_box);

  output->usable_area = output_box;
}

/* TODO: this needs tweaking in the future, rn outputs are not removed from
 * the layout, and workspaces and not updated. */
static void output_destroy(struct wl_listener *listener, void *data) {
	struct owl_output *output = wl_container_of(listener, output, destroy);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void change_workspace(struct owl_server *server, void *data) {
  struct owl_workspace *workspace = data;

  /* if it is the same as global active workspace, do nothing */
  if(server->active_workspace == workspace) return;

  /* if it is an already presented workspace on some other monitor, just switch to it */
  struct owl_output *o;
  wl_list_for_each(o, &server->outputs, link) {
    if(workspace == o->active_workspace) {
      server->active_workspace = workspace;
      cursor_jump_output(o);
      if(workspace->master != NULL) {
        focus_toplevel(workspace->master);
      } else {
        unfocus_focused_toplevel(server);
      }
      return;
    }
  }
  
  /* else remove all the toplevels on that workspace */
  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->output->active_workspace->floating_toplevels, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
  }

  if(workspace->output->active_workspace->master != NULL) {
    wlr_scene_node_set_enabled(
      &workspace->output->active_workspace->master->scene_tree->node, false);
    wl_list_for_each(t, &workspace->output->active_workspace->slaves, link) {
      wlr_scene_node_set_enabled(&t->scene_tree->node, false);
    }
  }

  /* and show this workspace's toplevels */
  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
  }

  if(workspace->master != NULL) {
    wlr_scene_node_set_enabled(&workspace->master->scene_tree->node, true);
    wl_list_for_each(t, &workspace->slaves, link) {
      wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    }
  }

  if(server->active_workspace->output != workspace->output) {
    cursor_jump_output(workspace->output);
  }

  server->active_workspace = workspace;
  workspace->output->active_workspace = workspace;
  if(workspace->master != NULL) {
    focus_toplevel(workspace->master);
  } else {
    unfocus_focused_toplevel(server);
  }
}

static void server_new_output(struct wl_listener *listener, void *data) {
  /* This event is raised by the backend when a new output (aka a display or
   * monitor) becomes available. */
  struct owl_server *server =
    wl_container_of(listener, server, new_output);
  struct wlr_output *wlr_output = data;

  /* Configures the output created by the backend to use our allocator
   * and our renderer. Must be done once, before commiting the output */
  wlr_output_init_render(wlr_output, server->allocator, server->renderer);

  /* The output may be disabled, switch it on. */
  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);

  bool set = false;
  struct monitor_config *m;
  wl_list_for_each(m, &server->config->monitors, link) {
    if(strcmp(m->name, wlr_output->name) == 0) {  
      wlr_log(WLR_DEBUG, "found: %s, set mode: %dx%d@%dmHz",
        m->name, m->width, m->height, m->refresh_rate);
      struct wlr_output_mode mode = {
        .width = m->width,
        .height = m->height,
        .refresh = m->refresh_rate,
      };
      wlr_output_state_set_mode(&state, &mode);
      set = true;
    }
  }

  if(!set) {
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
      wlr_output_state_set_mode(&state, mode);
    }
  }

  /* Atomically applies the new output state. */
  wlr_output_commit_state(wlr_output, &state);
  wlr_output_state_finish(&state);

  /* Allocates and configures our state for this output */
  struct owl_output *output = calloc(1, sizeof(*output));
  output->wlr_output = wlr_output;
  output->server = server;

  wlr_output->data = output;

  /* Sets up a listener for the frame event. */
  output->frame.notify = output_frame;
  wl_signal_add(&wlr_output->events.frame, &output->frame);

  /* Sets up a listener for the state request event. */
  output->request_state.notify = output_request_state;
  wl_signal_add(&wlr_output->events.request_state, &output->request_state);

  /* Sets up a listener for the destroy event. */
  output->destroy.notify = output_destroy;
  wl_signal_add(&wlr_output->events.destroy, &output->destroy);

  wl_list_init(&output->workspaces);

  for(size_t i = 0; i < server->config->workspaces_per_monitor; i++) {
    struct owl_workspace *workspace = calloc(1, sizeof(*workspace));
    wl_list_init(&workspace->slaves);
    wl_list_init(&workspace->floating_toplevels);
    workspace->master = NULL;
    workspace->output = output;
    workspace->index =
      wl_list_length(&server->outputs) * server->config->workspaces_per_monitor + i + 1;

    wl_list_insert(&output->workspaces, &workspace->link);

    /* if first then set it active */
    if(output->active_workspace == NULL) {
      output->active_workspace = workspace;
    }

    /* these binds should also be specified in the config, TODO: */
    struct keybind *change_workspace_i = calloc(1, sizeof(*change_workspace_i));
    *change_workspace_i = (struct keybind) {
      .modifiers = WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL,
      .sym = XKB_KEY_1 + workspace->index - 1,
      .action = change_workspace,
      .args = workspace,
    };

    wl_list_insert(&server->config->keybinds, &change_workspace_i->link);
  }
  
  /* if first output then set server's active workspace to this one */
  if(server->active_workspace == NULL) {
    server->active_workspace = output->active_workspace;
  }

  wl_list_init(&output->layers.background);
  wl_list_init(&output->layers.bottom);
  wl_list_init(&output->layers.top);
  wl_list_init(&output->layers.overlay);

  wl_list_insert(&server->outputs, &output->link);

  struct wlr_output_layout_output *l_output;
  set = false;
  wl_list_for_each(m, &server->config->monitors, link) {
    if(strcmp(m->name, wlr_output->name) == 0) {  
      wlr_log(WLR_DEBUG, "found: %s, set position: %d, %d", m->name, m->x, m->y);
      l_output = wlr_output_layout_add(server->output_layout, wlr_output, m->x, m->y);
      set = true;
    }
  }

  if(!set) {
    l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);
  }

  struct wlr_scene_output *scene_output =
    wlr_scene_output_create(server->scene, wlr_output);

  wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);

  struct wlr_box output_box;
  wlr_output_layout_get_box(output->server->output_layout, wlr_output, &output_box);
  output->usable_area = output_box;
}

static void clip_toplevel(struct owl_toplevel *toplevel, 
  uint32_t width, uint32_t height) {
  struct wlr_box clip = (struct wlr_box){
    .x = toplevel->xdg_toplevel->base->geometry.x,
    .y = toplevel->xdg_toplevel->base->geometry.y,
    .width = width,
    .height = height,
  };
  wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, &clip);
}

static void place_tiled_toplevels(struct owl_workspace *workspace) {
  struct owl_server *server = workspace->output->server;
  struct wlr_box output_box = workspace->output->usable_area;

  struct owl_toplevel *master = workspace->master;
  if(master == NULL) return;

  uint32_t outer_gaps = server->config->outer_gaps;
  uint32_t inner_gaps = server->config->inner_gaps;
  double master_ratio = server->config->master_ratio;
  double border_width = server->config->border_width;

  uint32_t master_width, master_height;
  uint32_t number_of_slaves = wl_list_length(&workspace->slaves);

  if(number_of_slaves > 0) {
    master_width = output_box.width * master_ratio - outer_gaps - inner_gaps - 2 * border_width;
    master_height = output_box.height - 2 * outer_gaps - 2 * border_width;
  } else {
    master_width = output_box.width  - 2 * outer_gaps - 2 * border_width;
    master_height = output_box.height - 2 * outer_gaps - 2 * border_width;
  }

  master->width = master_width;
  master->height = master_height;

  clip_toplevel(master, master_width, master_height);

  wlr_xdg_toplevel_set_size(master->xdg_toplevel, master_width, master_height);
  wlr_scene_node_set_position(&master->scene_tree->node, 
    output_box.x + outer_gaps + border_width, output_box.y + outer_gaps + border_width);
  toplevel_create_or_update_borders(master, master_width, master_height);

  if(number_of_slaves == 0) return;

  /* share the remaining space among slaves */
  struct owl_toplevel *t;
  uint32_t slave_width = output_box.width * (1 - master_ratio)
    - outer_gaps - inner_gaps - 2 * border_width;
  uint32_t slave_height = (output_box.height - 2 * outer_gaps
    - (number_of_slaves - 1) * inner_gaps * 2
    - number_of_slaves * 2 * border_width) / number_of_slaves;

  size_t i = 0;
  wl_list_for_each(t, &workspace->slaves, link) {
    t->width = slave_width;
    t->height = slave_height;
    clip_toplevel(t, slave_width, slave_height);
    wlr_xdg_toplevel_set_size(t->xdg_toplevel, slave_width, slave_height);
    wlr_scene_node_set_position(&t->scene_tree->node,
      output_box.x + output_box.width * master_ratio + inner_gaps + border_width,
      output_box.y + outer_gaps + border_width
        + i * (slave_height + inner_gaps * 2 + 2 * border_width));
    toplevel_create_or_update_borders(t, slave_width, slave_height);
    i++;
  }
}

/* this function assumes they are in the same workspace and that t2 comes after t1 in a list */
static void swap_tiled_toplevels(struct owl_toplevel *t1, struct owl_toplevel *t2) {
  if(t1 == t1->workspace->master) {
    t1->workspace->master = t2;
    wl_list_insert(&t2->link, &t1->link);
    wl_list_remove(&t2->link);
  } else if(t2 == t1->workspace->master) {
    t1->workspace->master = t1;
    wl_list_insert(&t1->link, &t2->link);
    wl_list_remove(&t1->link);
  } else {
    struct wl_list *before_t1 = t1->link.prev;
    wl_list_remove(&t1->link);
    wl_list_insert(&t2->link, &t1->link);
    wl_list_remove(&t2->link);
    wl_list_insert(before_t1, &t2->link);
  }

  place_tiled_toplevels(t1->workspace);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, map);
  struct owl_server *server = toplevel->server;

  wlr_log(WLR_ERROR, "new toplevel mapped: %p, title: %s", toplevel, toplevel->xdg_toplevel->title);
  struct wlr_scene_tree *scene_tree = toplevel->scene_tree;
  struct wlr_box output_box = toplevel->workspace->output->usable_area;

  uint32_t outer_gaps = server->config->outer_gaps;
  uint32_t inner_gaps = server->config->inner_gaps;

  if(toplevel->floating) {
	  wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);
    wlr_scene_node_set_position(&scene_tree->node,
      output_box.x + (output_box.width - toplevel->xdg_toplevel->base->geometry.width) / 2,
      output_box.y + (output_box.height - toplevel->xdg_toplevel->base->geometry.height) / 2);

    struct wlr_box geo_box = toplevel->xdg_toplevel->base->geometry;
    toplevel_create_or_update_borders(toplevel, geo_box.width, geo_box.height);
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  } else if(toplevel->workspace->master == NULL) {
    toplevel->workspace->master = toplevel;
    wlr_scene_node_lower_to_bottom(&toplevel->scene_tree->node);
    place_tiled_toplevels(toplevel->workspace);
  } else {
    wl_list_insert(&toplevel->workspace->slaves, &toplevel->link);
    wlr_scene_node_lower_to_bottom(&toplevel->scene_tree->node);
    place_tiled_toplevels(toplevel->workspace);
  }

	focus_toplevel(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
  struct owl_server *server = toplevel->server;

	/* Reset the cursor mode if the grabbed toplevel was unmapped. */
	if(toplevel == toplevel->server->grabbed_toplevel) {
		reset_cursor_mode(toplevel->server);
	}

  struct wlr_box output_box = toplevel->workspace->output->usable_area;

  if(toplevel->floating) {
	  wl_list_remove(&toplevel->link);
    return;
  }

  struct owl_toplevel *should_focus_next = NULL;
  if(toplevel == toplevel->workspace->master) {
    if(wl_list_empty(&toplevel->workspace->slaves)) {
      toplevel->workspace->master = NULL;
      return;
    } else {
      struct owl_toplevel *new_master = wl_container_of(
        toplevel->workspace->slaves.next, new_master, link);
      toplevel->workspace->master = new_master;
      /* remove him from the slaves list */
      wl_list_remove(&new_master->link);
      should_focus_next = new_master;
    }
  } else {
    /* take the previous toplevel in the slaves list. if its head (which means the to be */
    /* removed toplevel was the first) then instead take the next one. if its also head, */
    /* that means it was the only slave, so take the master. */
    should_focus_next = wl_container_of(toplevel->link.prev, should_focus_next, link);
    if(&should_focus_next->link == &toplevel->workspace->slaves) {
      should_focus_next = wl_container_of(toplevel->link.next, should_focus_next, link);
    }
    if(&should_focus_next->link == &toplevel->workspace->slaves) {
      should_focus_next = toplevel->workspace->master;
    }
    wl_list_remove(&toplevel->link);
  }
  
  place_tiled_toplevels(toplevel->workspace);
  focus_toplevel(should_focus_next);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	/* Called when a new surface state is committed. */
	struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

  if(!toplevel->xdg_toplevel->base->initialized) return;

	if(toplevel->xdg_toplevel->base->initial_commit) {
		/* When an xdg_surface performs an initial commit, the compositor must
		 * reply with a configure so the client can map the surface. */

    /* TODO: windowrules */
    toplevel->floating = false;

    if(toplevel->floating) {
      wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
      return;
    }

    struct wlr_box output_box = toplevel->workspace->output->usable_area;
    struct owl_server *server = toplevel->server;

    uint32_t outer_gaps = server->config->outer_gaps;
    uint32_t inner_gaps = server->config->inner_gaps;
    double master_ratio = server->config->master_ratio;

    if(toplevel->workspace->master == NULL) {
		  wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
        output_box.width - 2 * outer_gaps, output_box.height - 2 * outer_gaps);
    } else {
      uint32_t slave_width, slave_height;
      /* we add one (this one) to calculate what size should he take */
      uint32_t number_of_slaves = wl_list_length(&toplevel->workspace->slaves) + 1;
      slave_width = output_box.width * (1 - master_ratio) - outer_gaps - inner_gaps;
      slave_height = (output_box.height - 2 * outer_gaps
        - (number_of_slaves - 1) * inner_gaps * 2) / number_of_slaves;

      wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, slave_width, slave_height);
    }
    return;
  }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

	free(toplevel);
}

static uint32_t cursor_get_closest_toplevel_corner(struct wlr_cursor *cursor,
    struct owl_toplevel *toplevel) {
  struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
  int toplevel_x = toplevel->scene_tree->node.x + geo_box->x;
  int toplevel_y = toplevel->scene_tree->node.y + geo_box->y;

  int left_dist = cursor->x - toplevel_x;
  int right_dist = geo_box->width - left_dist;
  int top_dist = cursor->y - toplevel_y;
  int bottom_dist = geo_box->height - top_dist;

  uint32_t edges = 0;
  if(left_dist <= right_dist) {
    edges |= WLR_EDGE_LEFT;
  } else {
    edges |= WLR_EDGE_RIGHT;
  }

  if(top_dist <= bottom_dist) {
    edges |= WLR_EDGE_TOP;
  } else {
    edges |= WLR_EDGE_BOTTOM;
  }

  return edges;
}

static void begin_interactive(struct owl_toplevel *toplevel,
		enum owl_cursor_mode mode, uint32_t edges) {
  if(toplevel == NULL) {
    return;
  }

	struct owl_server *server = toplevel->server;
	if (toplevel != get_pointer_focused_toplevel(server)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

  server->grab_x = server->cursor->x;
  server->grab_y = server->cursor->y;

  struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
  server->grab_geobox = *geo_box;
  server->grab_geobox.x += toplevel->scene_tree->node.x;
  server->grab_geobox.y += toplevel->scene_tree->node.y;

	server->resize_edges = edges;
}

static void xdg_toplevel_request_move(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
  if(!toplevel->floating) return;

	begin_interactive(toplevel, OWL_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct owl_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
  if(!toplevel->floating) return;

	begin_interactive(toplevel, OWL_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on client-side
	 * decorations. owl doesn't support maximization, but to conform to
	 * xdg-shell protocol we still must send a configure.
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply.
	 * However, if the request was sent before an initial commit, we don't do
	 * anything and let the client finish the initial surface setup. */
	struct owl_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void xdg_toplevel_request_fullscreen(
		struct wl_listener *listener, void *data) {
	/* Just as with request_maximize, we must send a configure here. */
	struct owl_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	/* This event is raised when a client creates a new toplevel (application window). */
	struct owl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	/* Allocate a owl_toplevel for this surface */
	struct owl_toplevel *toplevel = calloc(1, sizeof(*toplevel));

	toplevel->server = server;
  toplevel->workspace = server->active_workspace;

	toplevel->xdg_toplevel = xdg_toplevel;
  
	toplevel->scene_tree =
		wlr_scene_xdg_surface_create(server->toplevel_tree, xdg_toplevel->base);
  /* we are keeping toplevels scene_tree in this free user data field, this is used in 
   * assigning parents to popups */
	xdg_toplevel->base->data = toplevel->scene_tree;

  /* in a node we want to keep information what that node represents. we do that
   * be keeping owl_something in user data field, which is a union of all possible
   * 'things' we can have on the screen */
	struct owl_something *something = calloc(1, sizeof(*something));
  something->type = OWL_TOPLEVEL;
  something->toplevel = toplevel;

	toplevel->scene_tree->node.data = something;

	/* Listen to the various events it can emit */
	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);

	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);

	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);

	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);

	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);

	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

static struct owl_output *toplevel_get_primary_output(struct owl_toplevel *toplevel) {
  uint32_t toplevel_x =
    toplevel->scene_tree->node.x +
    toplevel->xdg_toplevel->base->geometry.x;
  uint32_t toplevel_y =
    toplevel->scene_tree->node.y +
    toplevel->xdg_toplevel->base->geometry.y;

  struct wlr_box toplevel_box = {
    .x = toplevel_x,
    .y = toplevel_y,
    .width = toplevel->xdg_toplevel->base->geometry.width,
    .height = toplevel->xdg_toplevel->base->geometry.height,
  };

  struct wlr_box intersection_box;
  struct wlr_box output_box;
  uint32_t max_area = 0;
  struct owl_output *max_area_output = NULL;

  struct owl_output *o;
  wl_list_for_each(o, &toplevel->server->outputs, link) {
    wlr_output_layout_get_box(toplevel->server->output_layout, o->wlr_output, &output_box);
    bool intersects =
      wlr_box_intersection(&intersection_box, &toplevel_box, &output_box);
    if(intersects && box_area(&intersection_box) > max_area) {
      max_area = box_area(&intersection_box);
      max_area_output = o;
    }
  }

  return max_area_output;
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	/* Called when a new surface state is committed. */
	struct owl_popup *popup = wl_container_of(listener, popup, commit);

	if (!popup->xdg_popup->base->initialized) return;

	if (popup->xdg_popup->base->initial_commit) {
		/* When an xdg_surface performs an initial commit, the compositor must
		 * reply with a configure so the client can map the surface.
		 * owl sends an empty configure. A more sophisticated compositor
		 * might change an xdg_popup's geometry to ensure it's not positioned
		 * off-screen, for example. */
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_popup is destroyed. */
	struct owl_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

	free(popup);
}
/* TODO: change this so it can work with layer shell */
static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	/* This event is raised when a client creates a new popup. */
	struct wlr_xdg_popup *xdg_popup = data;

  wlr_log(WLR_ERROR, "new popup");

	struct owl_popup *popup = calloc(1, sizeof(*popup));
	popup->xdg_popup = xdg_popup;

  if(xdg_popup->parent != NULL) {
    struct wlr_xdg_surface *parent =
      wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent != NULL);
	  struct wlr_scene_tree *parent_tree = parent->data;
    popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    xdg_popup->base->data = popup->scene_tree;
    struct owl_something *something = calloc(1, sizeof(*something));
    something->type = OWL_POPUP;
    something->popup = popup;
    popup->scene_tree->node.data = something;
  } else {
    /* if there is no parent, than we keep the reference to our owl_popup state in this */
    /* user data pointer, in order to later reparent this popup (see new_layer_surface_popup) */
    xdg_popup->base->data = popup;
  }

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

static void handle_request_xdg_decoration(struct wl_listener *listener, void *data) {
  struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
  wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
    WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void stop_server(struct owl_server *server, void *data) {
	wl_display_terminate(server->wl_display);
}

static void exec(char *cmd) {
  if (fork() == 0) {
    execl("/bin/sh", "/bin/sh", "-c", cmd);
	}
}

static void run(struct owl_server *server, void *data) {
  if (fork() == 0) {
    execl("/bin/sh", "/bin/sh", "-c", (char*)data, (void *)NULL);
	}
}

static void resize_focused_toplevel(struct owl_server *server, void *data) {
  struct owl_toplevel *toplevel = get_pointer_focused_toplevel(server);

  if(toplevel == NULL || !toplevel->floating) return;

  uint32_t edges = cursor_get_closest_toplevel_corner(server->cursor, toplevel);

  char cursor_image[128] = {0};
  if(edges & WLR_EDGE_TOP) {
    strcat(cursor_image, "top_");
  } else {
    strcat(cursor_image, "bottom_");
  }
  if(edges & WLR_EDGE_LEFT) {
    strcat(cursor_image, "left_");
  } else {
    strcat(cursor_image, "right_");
  }
  strcat(cursor_image, "corner");

  wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, cursor_image);
  begin_interactive(toplevel, OWL_CURSOR_RESIZE, edges);
}

static void stop_resize_focused_toplevel(struct owl_server *server, void *data) {
  struct owl_output *primary_output = 
    toplevel_get_primary_output(server->grabbed_toplevel);

  if(primary_output != server->grabbed_toplevel->workspace->output) {
    server->grabbed_toplevel->workspace = primary_output->active_workspace;
    wl_list_remove(&server->grabbed_toplevel->link);
    wl_list_insert(&primary_output->active_workspace->floating_toplevels,
      &server->grabbed_toplevel->link);
  }

  reset_cursor_mode(server);
}

static void move_focused_toplevel(struct owl_server *server, void *data) {
  struct owl_toplevel *toplevel = get_pointer_focused_toplevel(server);
  if(toplevel == NULL || !toplevel->floating) return;

  wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "hand1");
  begin_interactive(toplevel, OWL_CURSOR_MOVE, 0);
}

static void stop_move_focused_toplevel(struct owl_server *server, void *data) {
  struct owl_output *primary_output = 
    toplevel_get_primary_output(server->grabbed_toplevel);

  if(primary_output != server->grabbed_toplevel->workspace->output) {
    server->grabbed_toplevel->workspace = primary_output->active_workspace;
    wl_list_remove(&server->grabbed_toplevel->link);
    wl_list_insert(&primary_output->active_workspace->floating_toplevels,
      &server->grabbed_toplevel->link);
  }

  reset_cursor_mode(server);
}

static void close_keyboard_focused_toplevel(struct owl_server *server, void *data) {
  struct owl_toplevel *toplevel = get_keyboard_focused_toplevel(server);
  if(toplevel == NULL) return;

  xdg_toplevel_send_close(toplevel->xdg_toplevel->resource);
}

static void move_focus(struct owl_server *server, void *data) {
  uint32_t direction = (uint32_t)data;

  struct owl_toplevel *toplevel = get_keyboard_focused_toplevel(server);

  if(toplevel == NULL || toplevel->floating) {
    struct wlr_output *wlr_output = wlr_output_layout_output_at(
      server->output_layout, server->cursor->x, server->cursor->y);
    struct owl_output *output = wlr_output->data;
    struct owl_output *relative_output = get_output_relative(server, output, direction);
    if(relative_output == NULL) return; 
    if(relative_output->active_workspace->master == NULL) {
      unfocus_focused_toplevel(server);
      server->active_workspace = relative_output->active_workspace;
      cursor_jump_output(relative_output);
      return;
    }
    focus_toplevel(relative_output->active_workspace->master);
    return;
  }
  
  if(toplevel == server->active_workspace->master) {
    switch (direction) {
      case RIGHT: {}
        if(!wl_list_empty(&server->active_workspace->slaves)) {
          struct owl_toplevel *first_slave =
            wl_container_of(server->active_workspace->slaves.next, first_slave, link);
          focus_toplevel(first_slave);
          return;
        }
      /* fallthrough is interntional; if there are no slaves then try right monitor */
      default: goto try_monitor;
    }
  }

  switch (direction) {
    case LEFT: {}
      focus_toplevel(server->active_workspace->master);
      return;

    case RIGHT: {}
      goto try_monitor;

    case UP: {}
      struct owl_toplevel *above = wl_container_of(toplevel->link.prev, above, link);
      if(&above->link != &server->active_workspace->slaves) {
        focus_toplevel(above);
        return;
      }
      goto try_monitor;

    case DOWN: {}
      struct owl_toplevel *bellow = wl_container_of(toplevel->link.next, bellow, link);
      if(&bellow->link != &server->active_workspace->slaves) {
        focus_toplevel(bellow);
        return;
      }
      goto try_monitor;
  }

try_monitor: {}
  struct owl_output *relative_output =
    get_output_relative(server, toplevel->workspace->output, direction);
  if(relative_output == NULL) return; 
  if(relative_output->active_workspace->master == NULL) {
    place_tiled_toplevels(relative_output->active_workspace);
    server->active_workspace = relative_output->active_workspace;
    cursor_jump_output(relative_output);
    unfocus_focused_toplevel(server);
    return;
  }
  focus_toplevel(relative_output->active_workspace->master);
  cursor_jump_focused_toplevel(server);
  return;
}

static void move_tiled_toplevel(struct owl_server *server, void *data) {
  uint32_t direction = (uint32_t)data;

  struct owl_toplevel *toplevel = 
    get_keyboard_focused_toplevel(server);

  if(toplevel == NULL || toplevel->floating) return;
  
  struct owl_workspace *active_workspace = server->active_workspace;
  if(toplevel == active_workspace->master) {
    switch (direction) {
      case RIGHT: {}
        if(!wl_list_empty(&active_workspace->slaves)) {
          struct owl_toplevel *first_slave =
            wl_container_of(active_workspace->slaves.next, first_slave, link);
          swap_tiled_toplevels(first_slave, active_workspace->master);
          return;
        }
      /* fallthrough is interntional; if there are no slaves then try right monitor */
      default: {}
        struct owl_output *relative_output =
          get_output_relative(server, active_workspace->output, direction);
        if(relative_output == NULL) return; 

        if(!wl_list_empty(&active_workspace->slaves)) {
          struct owl_toplevel *first_slave =
            wl_container_of(active_workspace->slaves.next, first_slave, link);
          toplevel->workspace->master = first_slave;
          wl_list_remove(&first_slave->link);
        } else {
          toplevel->workspace->master = NULL;
        }

        toplevel->workspace = relative_output->active_workspace;
        if(relative_output->active_workspace->master == NULL) {
          relative_output->active_workspace->master = toplevel;

          place_tiled_toplevels(relative_output->active_workspace);
          place_tiled_toplevels(active_workspace);

          server->active_workspace = relative_output->active_workspace;
          cursor_jump_focused_toplevel(server);
          return;
        }

        wl_list_insert(&relative_output->active_workspace->slaves, &toplevel->link);

        place_tiled_toplevels(relative_output->active_workspace);
        place_tiled_toplevels(server->active_workspace);

        server->active_workspace = relative_output->active_workspace;
        cursor_jump_focused_toplevel(server);
        return;
    }
  }

  switch (direction) {
    case LEFT: {}
      swap_tiled_toplevels(toplevel, server->active_workspace->master);
      return;

    case RIGHT: {}
      goto try_monitor_for_slave;

    case UP: {}
      struct owl_toplevel *above = wl_container_of(toplevel->link.prev, above, link);
      if(&above->link != &server->active_workspace->slaves) {
        swap_tiled_toplevels(above, toplevel);
        return;
      }
      goto try_monitor_for_slave;

    case DOWN: {}
      struct owl_toplevel *bellow = wl_container_of(toplevel->link.next, bellow, link);
      if(&bellow->link != &server->active_workspace->slaves) {
        swap_tiled_toplevels(toplevel, bellow);
        return;
      }
      goto try_monitor_for_slave;
  }

try_monitor_for_slave: {}
  struct owl_output *relative_output =
    get_output_relative(server, toplevel->workspace->output, direction);
  if(relative_output == NULL) return; 
  if(relative_output->active_workspace->master == NULL) {
    wl_list_remove(&toplevel->link);
    relative_output->active_workspace->master = toplevel;
    toplevel->workspace = relative_output->active_workspace;

    place_tiled_toplevels(relative_output->active_workspace);
    place_tiled_toplevels(server->active_workspace);
    server->active_workspace = relative_output->active_workspace;
    return;
  }
  wl_list_remove(&toplevel->link);
  wl_list_insert(&relative_output->active_workspace->slaves, &toplevel->link);
  toplevel->workspace = relative_output->active_workspace;

  place_tiled_toplevels(relative_output->active_workspace);
  place_tiled_toplevels(server->active_workspace);
  server->active_workspace = relative_output->active_workspace;
  return;
}

static void switch_focused_toplevel_state(struct owl_server *server, void *data) {
  struct owl_toplevel *toplevel = get_keyboard_focused_toplevel(server);
  if(toplevel == NULL) return;

  if(toplevel->floating) {
    toplevel->floating = false;
    wl_list_remove(&toplevel->link);

    if(toplevel->workspace->master == NULL) {
      toplevel->workspace->master = toplevel;
    } else {
      wl_list_insert(&toplevel->workspace->slaves, &toplevel->link);
    }

    place_tiled_toplevels(toplevel->workspace);
    return;
  }

  toplevel->floating = true;
  if(toplevel == toplevel->workspace->master) {
    if(wl_list_empty(&toplevel->workspace->slaves)) {
      toplevel->workspace->master = NULL;
    } else {
      struct owl_toplevel *new_master = wl_container_of(
        toplevel->workspace->slaves.next, new_master, link);
      toplevel->workspace->master = new_master;
      /* remove him from the slaves list */
      wl_list_remove(&new_master->link);
    }
  } else {
    wl_list_remove(&toplevel->link);
  }

  clip_toplevel(toplevel, INT32_MAX, INT32_MAX);

  uint32_t width = toplevel->xdg_toplevel->base->geometry.width;
  uint32_t height = toplevel->xdg_toplevel->base->geometry.height;

  toplevel_create_or_update_borders(toplevel, width, height);

  struct wlr_box output_box = toplevel->workspace->output->usable_area;

  wlr_scene_node_set_position(&toplevel->scene_tree->node,
    output_box.x + (output_box.width - width) / 2,
    output_box.y + (output_box.height - height) / 2);

  wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);

  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  place_tiled_toplevels(toplevel->workspace);
}

static void handle_layer_surface_commit(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, commit);

  if (!layer_surface->wlr_layer_surface->initialized) {
    return;
  }

  if(layer_surface->wlr_layer_surface->initial_commit) {
    struct owl_output *output = layer_surface->wlr_layer_surface->output->data;

    struct wlr_box output_box;
    wlr_output_layout_get_box(output->server->output_layout,
      output->wlr_output, &output_box);

    struct wlr_box temp = output->usable_area;
		wlr_scene_layer_surface_v1_configure(layer_surface->scene_tree, &output_box, &output->usable_area);
  }
}

static void handle_layer_surface_map(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, map);
  struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->wlr_layer_surface;

  enum zwlr_layer_shell_v1_layer layer = wlr_layer_surface->pending.layer;
  struct owl_output *output = wlr_layer_surface->output->data;

  wlr_scene_node_raise_to_top(&layer_surface->scene_tree->tree->node);

  switch (layer) {
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
  wlr_output_layout_get_box(output->server->output_layout,
    output->wlr_output, &output_box);

  struct wlr_box temp = output->usable_area;
  wlr_scene_layer_surface_v1_configure(layer_surface->scene_tree, &output_box, &output->usable_area);

  if(temp.width != output->usable_area.width || temp.height != output->usable_area.height) {
    place_tiled_toplevels(output->active_workspace);
  }

  if(focus_layer_surface(layer_surface)) {
    unfocus_focused_toplevel(layer_surface->server);
  }
}

static void handle_layer_surface_unmap(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, unmap);
  struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->wlr_layer_surface;
  struct wlr_layer_surface_v1_state *state = &layer_surface->wlr_layer_surface->current;
  struct owl_server *server = layer_surface->server;
  struct owl_output *output = layer_surface->wlr_layer_surface->output->data;

  if(layer_surface == server->layer_exlusive_keyboard) {
    server->layer_exlusive_keyboard = NULL;
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
    place_tiled_toplevels(output->active_workspace);
  }

  wl_list_remove(&layer_surface->link);
  if(output->active_workspace->master != NULL) {
    focus_toplevel(output->active_workspace->master);
  }
}

static void handle_layer_surface_destroy(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, destroy);

  wl_list_remove(&layer_surface->map.link);
  wl_list_remove(&layer_surface->unmap.link);
  wl_list_remove(&layer_surface->destroy.link);

  free(layer_surface);
}

static void handle_new_layer_surface_popup(struct wl_listener *listener, void *data) {
  struct owl_layer_surface *layer_surface = wl_container_of(listener, layer_surface, new_popup);
  struct wlr_xdg_popup *xdg_popup = data;

  /* see server_new_xdg_popup */
  struct owl_popup *popup = xdg_popup->base->data;

  struct wlr_scene_tree *parent_tree = layer_surface->scene_tree->tree;
  popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
}

static void handle_new_layer_surface(struct wl_listener *listener, void *data) {
  struct owl_server *server = wl_container_of(listener, server, new_layer_surface);
  struct wlr_layer_surface_v1 *wlr_layer_surface = data;

  struct owl_layer_surface *layer_surface = calloc(1, sizeof(*layer_surface));
  layer_surface->wlr_layer_surface = wlr_layer_surface;
  layer_surface->wlr_layer_surface->data = layer_surface;
  layer_surface->server = server;

  enum zwlr_layer_shell_v1_layer layer = wlr_layer_surface->pending.layer;
  switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
      layer_surface->scene_tree =
        wlr_scene_layer_surface_v1_create(server->background_tree, wlr_layer_surface);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
      layer_surface->scene_tree =
        wlr_scene_layer_surface_v1_create(server->bottom_tree, wlr_layer_surface);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
      layer_surface->scene_tree =
        wlr_scene_layer_surface_v1_create(server->top_tree, wlr_layer_surface);
      break;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
      layer_surface->scene_tree =
        wlr_scene_layer_surface_v1_create(server->overlay_tree, wlr_layer_surface);
      break;
  }

	struct owl_something *something = calloc(1, sizeof(*something));
  something->type = OWL_LAYER_SURFACE;
  something->layer_surface = layer_surface;

	layer_surface->scene_tree->tree->node.data = something;

  if (layer_surface->wlr_layer_surface->output == NULL) {
    struct owl_output *output = wl_container_of(server->outputs.next, output, link);
    layer_surface->wlr_layer_surface->output = output->wlr_output;
  }

  layer_surface->commit.notify = handle_layer_surface_commit;
  wl_signal_add(&wlr_layer_surface->surface->events.commit, &layer_surface->commit);

  layer_surface->map.notify = handle_layer_surface_map;
  wl_signal_add(&wlr_layer_surface->surface->events.map, &layer_surface->map);

  layer_surface->unmap.notify = handle_layer_surface_unmap;
  wl_signal_add(&wlr_layer_surface->surface->events.unmap, &layer_surface->unmap);

  layer_surface->new_popup.notify = handle_new_layer_surface_popup;
  wl_signal_add(&wlr_layer_surface->events.new_popup, &layer_surface->new_popup);

  layer_surface->destroy.notify = handle_layer_surface_destroy;
  wl_signal_add(&wlr_layer_surface->surface->events.destroy, &layer_surface->destroy);
}

static bool config_add_keybind(struct owl_config *c, char *modifiers, char *key,
    char* action, char **args, size_t arg_count) {
  char *p = modifiers;
  uint32_t modifiers_flag = 0;

  while(*p != '\0') {
    char mod[64] = {0};
    char *q = mod;
    while(*p != '+' && *p != '\0') {
      *q = *p;
      p++;
      q++;
    }

    if(strcmp(mod, "alt") == 0) {
      modifiers_flag |= WLR_MODIFIER_ALT;
    } else if(strcmp(mod, "super") == 0) {
      modifiers_flag |= WLR_MODIFIER_LOGO;
    } else if(strcmp(mod, "ctrl") == 0) {
      modifiers_flag |= WLR_MODIFIER_CTRL;
    } else if(strcmp(mod, "shift") == 0) {
      modifiers_flag |= WLR_MODIFIER_SHIFT;
    }

    if(*p == '+') {
      p++;
    }
  }

  if(modifiers_flag == 0) {
    wlr_log(WLR_ERROR, "modifiers %s don't seem to match any known modifiers", modifiers);
    return false;
  }

  uint32_t key_sym = 0;
  if(strcmp(key, "return") == 0 || strcmp(key, "enter") == 0) {
    key_sym = XKB_KEY_Return;
  } else if(strcmp(key, "backspace") == 0) {
    key_sym = XKB_KEY_BackSpace;
  } else if(strcmp(key, "delete") == 0) {
    key_sym = XKB_KEY_Delete;
  } else if(strcmp(key, "escape") == 0) {
    key_sym = XKB_KEY_Escape;
  } else if(strcmp(key, "tab") == 0) {
    key_sym = XKB_KEY_Tab;
  } else {
    key_sym = xkb_keysym_from_name(key, 0);
    if(key_sym == 0) {
      wlr_log(WLR_ERROR, "key %s doesn't seem right", key);
      return false;
    }
  }

  /*wlr_log(WLR_ERROR, "keysym is %u", key_sym);*/
  struct keybind *k = calloc(1, sizeof(*k));
  *k = (struct keybind){
    .modifiers = modifiers_flag,
    .sym = key_sym,
  };

  if(strcmp(action, "exit") == 0) {
    k->action = stop_server;
  } else if(strcmp(action, "run") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    k->action = run;
    k->args = args[0];
  } else if(strcmp(action, "kill_active") == 0) {
    k->action = close_keyboard_focused_toplevel;
  } else if(strcmp(action, "switch_floating_state") == 0) {
    k->action = switch_focused_toplevel_state;
  } else if(strcmp(action, "resize") == 0) {
    k->action = resize_focused_toplevel;
    k->stop = stop_resize_focused_toplevel;
  } else if(strcmp(action, "move") == 0) {
    k->action = move_focused_toplevel;
    k->stop = stop_move_focused_toplevel;
  } else if(strcmp(action, "move_focus") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    enum owl_direction direction;
    if(strcmp(args[0], "up") == 0) {
      direction = UP;
    } else if(strcmp(args[0], "left") == 0) {
      direction = LEFT;
    } else if(strcmp(args[0], "down") == 0) {
      direction = DOWN;
    } else if(strcmp(args[0], "right") == 0) {
      direction = RIGHT;
    } else {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    k->action = move_focus;
    k->args = (void*)direction;
  } else if(strcmp(action, "swap") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    enum owl_direction direction;
    if(strcmp(args[0], "up") == 0) {
      direction = UP;
    } else if(strcmp(args[0], "left") == 0) {
      direction = LEFT;
    } else if(strcmp(args[0], "down") == 0) {
      direction = DOWN;
    } else if(strcmp(args[0], "right") == 0) {
      direction = RIGHT;
    } else {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    k->action = move_tiled_toplevel;
    k->args = (void*)direction;
  } 

  wl_list_insert(&c->keybinds, &k->link);
  return true;
}

static bool handle_config_value(struct owl_config *c, char* keyword, char **args, size_t arg_count) {
  if(strcmp(keyword, "min_toplevel_size") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->min_toplevel_size = atoi(args[0]);
  } else if(strcmp(keyword, "workspaces_per_monitor") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->workspaces_per_monitor = atoi(args[0]);
  } else if(strcmp(keyword, "keyboard_rate") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->keyboard_rate = atoi(args[0]);
  } else if(strcmp(keyword, "keyboard_delay") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->keyboard_delay = atoi(args[0]);
  } else if(strcmp(keyword, "natural_scroll") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->natural_scroll = atoi(args[0]);
  } else if(strcmp(keyword, "tap_to_click") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->tap_to_click = atoi(args[0]);
  } else if(strcmp(keyword, "border_width") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->border_width = atoi(args[0]);
  } else if(strcmp(keyword, "outer_gaps") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->outer_gaps = atoi(args[0]);
  } else if(strcmp(keyword, "inner_gaps") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->inner_gaps = atoi(args[0]);
  } else if(strcmp(keyword, "master_ratio") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->master_ratio = atof(args[0]);
  } else if(strcmp(keyword, "cursor_theme") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->cursor_theme = args[0];
  } else if(strcmp(keyword, "inactive_border_color") == 0) {
    if(arg_count < 4) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->inactive_border_color[0] = atoi(args[0]) / 256.0;
    c->inactive_border_color[1] = atoi(args[1]) / 256.0;
    c->inactive_border_color[2] = atoi(args[2]) / 256.0;
    c->inactive_border_color[3] = atoi(args[3]) / 256.0;
  } else if(strcmp(keyword, "active_border_color") == 0) {
    if(arg_count < 4) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    c->active_border_color[0] = atoi(args[0]) / 256.0;
    c->active_border_color[1] = atoi(args[1]) / 256.0;
    c->active_border_color[2] = atoi(args[2]) / 256.0;
    c->active_border_color[3] = atoi(args[3]) / 256.0;
  } else if(strcmp(keyword, "monitor") == 0) {
    if(arg_count < 6) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    struct monitor_config *m = calloc(1, sizeof(*m));
    *m = (struct monitor_config){
      .name = args[0],
      .x = atoi(args[1]),
      .y = atoi(args[2]),
      .width = atoi(args[3]),
      .height = atoi(args[4]),
      .refresh_rate = atoi(args[5]) * 1000,
    };
    wl_list_insert(&c->monitors, &m->link);
  } else if(strcmp(keyword, "exec") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    exec(args[0]);
  } else if(strcmp(keyword, "keybind") == 0) {
    if(arg_count < 3) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      return false;
    }
    config_add_keybind(c, args[0], args[1], args[2], &args[3], arg_count - 3);
  } else {
    wlr_log(WLR_ERROR, "invalid keyword %s", keyword);
    return false;
  }

  return true;
}

static void load_default_config(struct owl_config *c) {
  c->min_toplevel_size = 100;
  c->workspaces_per_monitor = 5;
  c->cursor_theme = "Bibata-Modern-Ice";
  c->keyboard_rate = 30;
  c->keyboard_delay = 200;
  c->natural_scroll = true;
  c->tap_to_click = true;

  float inactive_col[4] = { 1, 1, 1, 1 };
  c->inactive_border_color[0] = inactive_col[0];
  c->inactive_border_color[1] = inactive_col[1];
  c->inactive_border_color[2] = inactive_col[2];
  c->inactive_border_color[3] = inactive_col[3];

  float active_col[4] = { 0, 0, 0, 1.0 };
  c->active_border_color[0] = active_col[0];
  c->active_border_color[1] = active_col[1];
  c->active_border_color[2] = active_col[2];
  c->active_border_color[3] = active_col[3];

  c->border_width = 2;
  c->outer_gaps = 32;
  c->inner_gaps = 16;
  c->master_ratio = 0.65;

  wl_list_init(&c->monitors);
  wl_list_init(&c->keybinds);
}

static bool server_load_config(struct owl_server *server) {
  struct owl_config *c = calloc(1, sizeof(*c));

  load_default_config(c);

  char config_path[512];
  char *config_home = getenv("XDG_CONFIG_HOME");
  if(config_home == NULL) {
    /* default to $HOME/.config if XDG_CONFIG_HOME is not set */
    char *home = getenv("HOME");
    if(home == NULL) {
      wlr_log(WLR_ERROR, "couldn't open config file");
      return false;
    }

    snprintf(config_path, sizeof(config_path), "%s/.config/owl/owl.conf", home);
  } else {
    snprintf(config_path, sizeof(config_path), "%s/owl/owl.conf", config_home);
  }

  FILE *config_file = fopen(config_path, "r");
  if(config_file == NULL) {
    wlr_log(WLR_ERROR, "couldn't open config file at %s", config_path);
    return false;
  }
  
  char line_buffer[1024] = {0};
  while(fgets(line_buffer, 1024, config_file)) {
    if(line_buffer[0] == '\n') {
      continue; 
    }

    char keyword[64] = {0};
    char *args[8];
    uint32_t safe_counter = 0;

    char *p = line_buffer;
    while(*p == ' ') p++;

    char *q = keyword;
    while(*p != ' ') {
      if(safe_counter > 63) {
        wlr_log(WLR_ERROR, "keyword %s too long", keyword);
        return false;
      }
      *q = *p;
      p++;
      q++;
      safe_counter++;
    }
    safe_counter = 0;

    while(*p == ' ') p++;

    if(*p == '\n') {
      wlr_log(WLR_ERROR, "no args provided for %s", keyword);
      return false;
    }

    size_t args_counter = 0;
    while(true) {
      args[args_counter] = calloc(64, sizeof(char));
      q = args[args_counter];

      bool word = false;
      if(*p == '\"') {
        word = true;
        p++;
      };

      while((word && *p != '\"') || (!word && *p != ' ' && *p != '\n')) {
        if(safe_counter > 63) {
          wlr_log(WLR_ERROR, "arg %s too long", args[args_counter]);
          return false;
        }
        *q = *p;
        p++;
        q++;
        safe_counter++;
      }
      args_counter++;

      if(word) p++;

      while(*p == ' ') p++;

      if(*p == '\n') break;
      if(args_counter == 8) {
        wlr_log(WLR_ERROR, "too many args to %s", keyword);
        return false;
      }
    }
    handle_config_value(c, keyword, args, args_counter);
  }

  fclose(config_file);

  wlr_log(WLR_ERROR, "keyboard_rate %d", c->keyboard_rate);
  wlr_log(WLR_ERROR, "keyboard_delay %d", c->keyboard_delay);
  wlr_log(WLR_ERROR, "cursor_theme %s", c->cursor_theme);
  wlr_log(WLR_ERROR, "workspaces_per_monitor %d", c->workspaces_per_monitor);
  wlr_log(WLR_ERROR, "border_width %d", c->border_width);
  wlr_log(WLR_ERROR, "outer_gaps %d", c->outer_gaps);
  wlr_log(WLR_ERROR, "inner_gaps %d", c->inner_gaps);
  wlr_log(WLR_ERROR, "master_ratio %.3lf", c->master_ratio);
  wlr_log(WLR_ERROR, "natural_scroll %d", c->natural_scroll);
  wlr_log(WLR_ERROR, "tap_to_click %d", c->tap_to_click);

  server->config = c;

  return true;
}

static void server_destroy_config(struct owl_server *server) {
  struct monitor_config *m, *tmp_m;

  wl_list_for_each_safe(m, tmp_m, &server->config->monitors, link) {
    wl_list_remove(&m->link);
    free(m);
  }

  assert(wl_list_empty(&server->config->monitors));

  struct keybind *k, *tmp_k;

  wl_list_for_each_safe(k, tmp_k, &server->config->keybinds, link) {
    wl_list_remove(&k->link);
    free(k);
  }

  assert(wl_list_empty(&server->config->keybinds));

  free(server->config);
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_ERROR, NULL);

	struct owl_server server = {0};

  bool valid_config = server_load_config(&server);
  if(!valid_config) {
    wlr_log(WLR_ERROR, "invalid config");
    return 1;
  }

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	server.wl_display = wl_display_create();

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return 1;
	}

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		return 1;
	}

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	server.allocator = wlr_allocator_autocreate(server.backend,
		server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return 1;
	}

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces, the subcompositor allows to
	 * assign the role of subsurfaces to surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the handling of the request_set_selection event below.*/
	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	server.output_layout = wlr_output_layout_create(server.wl_display);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

  /* create a manager used for comunicating with the clients */
  server.xdg_output_manager = wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);

	/* Create a scene graph. This is a wlroots abstraction that handles all
	 * rendering and damage tracking. All the compositor author needs to do
	 * is add things that should be rendered to the scene graph at the proper
	 * positions and then call wlr_scene_output_commit() to render a frame if
	 * necessary.
	 */
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

  server.background_tree = wlr_scene_tree_create(&server.scene->tree);
  server.bottom_tree = wlr_scene_tree_create(&server.scene->tree);
  server.toplevel_tree = wlr_scene_tree_create(&server.scene->tree);
  server.top_tree = wlr_scene_tree_create(&server.scene->tree);
  server.overlay_tree = wlr_scene_tree_create(&server.scene->tree);

	/* Set up xdg-shell version 6. The xdg-shell is a Wayland protocol which is
	 * used for application windows. For more detail on shells, refer to
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html.
	 */
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 6);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

  server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 5);
  server.new_layer_surface.notify = handle_new_layer_surface;
  server.layer_shell->data = &server;
  wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). */
	server.cursor_mgr = wlr_xcursor_manager_create(server.config->cursor_theme, 24);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html.
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */

	server.cursor_mode = OWL_CURSOR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute,
		&server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_list_init(&server.keyboards);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor,
		&server.request_cursor);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection,
		&server.request_set_selection);

  /* handles clipboard clients */
  server.data_control_manager = wlr_data_control_manager_v1_create(server.wl_display);

  /* configures decorations */
  server.xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server.wl_display);

  server.request_xdg_decoration.notify = handle_request_xdg_decoration;
  wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration,
    &server.request_xdg_decoration);

  server.viewporter = wlr_viewporter_create(server.wl_display);

	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	/* Set the WAYLAND_DISPLAY environment variable to our socket */
	setenv("WAYLAND_DISPLAY", socket, true);

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
			socket);
	wl_display_run(server.wl_display);

	/* Once wl_display_run returns, we destroy all clients then shut down the
	 * server. */
  server_destroy_config(&server);
	wl_display_destroy_clients(server.wl_display);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
	return 0;
}
