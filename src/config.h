#pragma once

#include <libinput.h>
#include <regex.h>
#include <scenefx/types/fx/blur_data.h>
#include <scenefx/types/fx/corner_location.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "animations.h"
#include "decoration.h"
#include "mwc.h"

#define BAKED_POINTS_COUNT 256

enum decoration_provider {
    DECORATION_PROVIDER_NONE,
    DECORATION_PROVIDER_CLIENT,
    DECORATION_PROVIDER_SERVER,
};

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

struct window_rule_no_titlebar {
    struct window_rule_regex condition;
    struct wl_list link;
};

struct layer_rule_regex {
    bool has;
    regex_t regex;
};

struct layer_rule_blur {
    struct layer_rule_regex condition;
    struct wl_list link;
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
    char *output;
    uint32_t index;
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
#define WITH_SPECIFIED(type) \
    struct {                 \
        type value;          \
        bool specified;      \
    }

struct mwc_config {
    // NULL if default config
    char *dir;

    // todo: make some of these hash maps or arrays for faster lookups
    struct wl_list outputs;
    struct wl_list keybinds;  // especially this one, because its currently looping through a whole linked list
    struct wl_list pointer_keybinds;
    struct wl_list workspaces;
    struct {
        struct wl_list floating;
        struct wl_list size;
        struct wl_list opacity;
        struct wl_list no_titlebar;
    } window_rules;

    struct {
        struct wl_list blur;
    } layer_rules;

    // keyboard stuff
    char *keymap_layouts;
    char *keymap_variants;
    char *keymap_options;
    uint32_t keyboard_rate;
    uint32_t keyboard_delay;

    // pointer stuff
    double pointer_sensitivity;
    enum libinput_config_accel_profile pointer_acceleration;
    struct wl_list pointers;
    bool pointer_left_handed;

    // trackpad stuff
    bool trackpad_disable_while_typing;
    bool trackpad_natural_scroll;
    bool trackpad_tap_to_click;
    enum libinput_config_scroll_method trackpad_scroll_method;

    // cursor theme and size
    char *cursor_theme;
    uint32_t cursor_size;

    // general toplevel and layout stuff
    uint32_t toplevel_minimum_needed_width;
    double inactive_opacity;
    double active_opacity;
    bool apply_opacity_when_fullscreen;
    uint32_t outer_gaps;
    uint32_t inner_gaps;

    uint32_t master_count;
    double master_ratio;

    // decorations
    enum decoration_provider decoration_provider;
    struct decoration_config decoration;

    bool blur;
    struct blur_data blur_params;

    bool shadows;

    // animations stuff
    bool animations;
    uint32_t animation_duration;
    struct fx_animation_curve *animation_curve;

    // run on startup
    char *run[64];
    size_t run_count;
};

struct mwc_config *
config_load();

void
config_reload();

void
config_destroy(struct mwc_config *c);

void *
config_watch(void *data);
