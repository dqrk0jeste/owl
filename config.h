#pragma once

#include <wayland-util.h>

#define MIN_TOPLEVEL_SIZE 100

struct owl_config {
  uint32_t mod;
  struct wl_list monitors;
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
