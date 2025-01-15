#pragma once

#include "keyboard.h"

#include <wayland-server-core.h>

typedef void (*keybind_action_func_t)(void *);

struct keybind {
  bool initialized;
  uint32_t modifiers;
  uint32_t key;
  keybind_action_func_t action;
  bool active;
  keybind_action_func_t stop;
  void *args;
  struct wl_list link;
};

bool
server_handle_keybinds(struct owl_keyboard *keyboard,
                       uint32_t keycode,
                       enum wl_keyboard_key_state state);

bool
server_handle_mouse_keybinds(uint32_t button, enum wl_pointer_button_state state);

bool
handle_change_vt_key(const xkb_keysym_t *keysyms, size_t count);

void
keybind_stop_server(void *data);

void
keybind_run(void *data);

void
keybind_change_workspace(void *data);

void
keybind_next_workspace(void *data);

void
keybind_prev_workspace(void *data);

void
keybind_move_focused_toplevel_to_workspace(void *data);

void
keybind_resize_focused_toplevel(void *data);

void
keybind_stop_resize_focused_toplevel(void *data);

void
keybind_move_focused_toplevel(void *data);

void
keybind_stop_move_focused_toplevel(void *data);

void
keybind_close_keyboard_focused_toplevel(void *data);

void
keybind_move_focus(void *data);

void
keybind_swap_focused_toplevel(void *data);

void
keybind_switch_focused_toplevel_state(void *data);
