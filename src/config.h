#pragma once

#include "helpers.h"
#include "mwc.h"
#include "animations.h"

#include <scenefx/types/fx/blur_data.h>
#include <scenefx/types/fx/corner_location.h>

#include <libinput.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#define BAKED_POINTS_COUNT 256

enum mwc_decorations {
    MWC_DECORATIONS_NONE,
    MWC_DECORATIONS_CLIENT_SIDE,
    MWC_DECORATIONS_SERVER_SIDE,
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

struct mwc_config {
    // NULL if default config
    char *dir;

    struct wl_list outputs;
    struct wl_list keybinds;
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
    struct mwc_color inactive_border_color;
    struct mwc_color active_border_color;
    double inactive_opacity;
    double active_opacity;
    bool apply_opacity_when_fullscreen;
    uint32_t border_width;
    uint32_t outer_gaps;
    uint32_t inner_gaps;

    // eye-candy
    uint32_t border_radius;
    enum corner_location border_radius_location;
    bool blur;
    struct blur_data blur_params;
    bool shadows;
    uint32_t shadows_size;
    struct {
        int32_t x;
        int32_t y;
    } shadows_position;
    struct mwc_color shadows_color;
    double shadows_blur;

    uint32_t master_count;
    double master_ratio;

    enum mwc_decorations decorations;

    // titlebar stuff
    uint32_t titlebar_height;
    struct mwc_color titlebar_color_active;
    struct mwc_color titlebar_color_inactive;
    bool titlebar_include_close_button;
    uint32_t titlebar_close_button_size;
    uint32_t titlebar_close_button_padding_left;
    uint32_t titlebar_close_button_padding_right;
    bool titlebar_close_button_square;
    bool titlebar_close_button_left;
    struct mwc_color titlebar_close_button_color_active;
    struct mwc_color titlebar_close_button_color_inactive;
    bool titlebar_include_title;
    bool titlebar_center_title;
    uint32_t titlebar_title_padding_left;
    uint32_t titlebar_title_padding_right;
    struct mwc_color titlebar_title_color;
    // will be generated from the name specified by `titlebar_title_font`, may be NULL
    struct fcft_font *font;

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

