#pragma once

#include <wayland-util.h>
#include <stdbool.h>

#define MIN_TOPLEVEL_SIZE 100

struct owl_config {
  struct wl_list monitors;
  struct wl_list keybinds;
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
