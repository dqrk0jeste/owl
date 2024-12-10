#pragma once

#include "keyboard.h"

#include <wayland-server-core.h>

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

bool
server_handle_keybinds(struct owl_keyboard *keyboard,
                       uint32_t keycode,
                       enum wl_keyboard_key_state state);

void
keybind_stop_server(void *data);

void
keybind_run(void *data);

void
keybind_change_workspace(void *data);

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