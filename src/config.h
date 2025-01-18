#pragma once

#include "helpers.h"

#include <libinput.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#define BAKED_POINTS_COUNT 256

struct window_rule_regex {
  bool has_app_id_regex;
  regex_t app_id_regex;
  bool has_title_regex;
  regex_t title_regex;
};

struct window_rule_float {
  struct window_rule_regex condition;
  struct wl_list link;
};

struct window_rule_size {
  struct window_rule_regex condition;
  struct wl_list link;
  bool relative_width;
  uint32_t width;
  bool relative_height;
  uint32_t height;
};

struct window_rule_opacity {
  struct window_rule_regex condition;
  struct wl_list link;
  double inactive_value;
  double active_value;
};

struct output_config {
  char *name;
  struct wl_list link;
  uint32_t width;
  uint32_t height;
  uint32_t refresh_rate;
  uint32_t x;
  uint32_t y;
  double scale;
};

struct workspace_config {
  uint32_t index;
  char *output;
  struct wl_list link;
};

struct pointer_config {
  char *name;
  double sensitivity;
  enum libinput_config_accel_profile acceleration;
  struct wl_list link;
};

/* we usually can tell if an option is specified or not by comparing them to 0 (or NULL),
 * but sometimes 0 can also mean something else. for such options we add another bool value
 * to tell if they are specified or not. */
#define WITH_SPECIFIED(type) struct { \
  type value;                         \
  bool specified;                     \
}                                     \
/* not used currently but may be needed in the future */

struct owl_config {
  struct wl_list outputs;
  struct wl_list keybinds;
  struct wl_list pointer_keybinds;
  struct wl_list workspaces;
  struct {
    struct wl_list floating;
    struct wl_list size;
    struct wl_list opacity;
  } window_rules;

  /* keyboard stuff */
  char *keymap_layouts;
  char *keymap_variants;
  char *keymap_options;
  uint32_t keyboard_rate;
  uint32_t keyboard_delay;

  /* pointer stuff */
  double pointer_sensitivity;
  bool pointer_acceleration;
  struct wl_list pointers;
  bool pointer_left_handed;

  /* trackpad stuff */
  bool trackpad_disable_while_typing;
  bool trackpad_natural_scroll;
  bool trackpad_tap_to_click;
  enum libinput_config_scroll_method trackpad_scroll_method;

  /* cursor theme and size */
  char *cursor_theme;
  uint32_t cursor_size;

  /* general toplevel and layout stuff */
  uint32_t min_toplevel_size;
  float inactive_border_color[4];
  float active_border_color[4];
  double inactive_opacity;
  double active_opacity;
  uint32_t border_width;
  uint32_t outer_gaps[4];
  uint32_t inner_gaps;
  uint32_t master_count;
  double master_ratio;
  bool client_side_decorations;

  /* animations stuff */
  bool animations;
  uint32_t animation_duration;
  double animation_curve[4];
  struct vec2 *baked_points;
  float placeholder_color[4];

  /* run on startup */
  char *run[64];
  size_t run_count;
};

struct vec2
calculate_animation_curve_at(struct owl_config *c, double t);

void
bake_bezier_curve_points(struct owl_config *c);

bool
config_add_window_rule(struct owl_config *c, char *app_id_regex, char *title_regex,
                       char *predicate, char **args, size_t arg_count);

bool
config_add_keybind(struct owl_config *c, char *modifiers, char *key,
                   char* action, char **args, size_t arg_count);

void
config_free_args(char **args, size_t arg_count);

bool
config_handle_value(struct owl_config *c, char *keyword, char **args, size_t arg_count);

FILE *
try_open_config_file();

/* assumes the line is newline teriminated, as it should be with fgets() */
bool
config_handle_line(char *line, size_t line_number, char **keyword,
                   char ***args, size_t *args_count);

bool
server_load_config();

void
config_set_default_needed_params(struct owl_config *c);
