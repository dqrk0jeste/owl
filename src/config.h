#pragma once

#include <libinput.h>
#include <regex.h>
#include <scenefx/types/fx/blur_data.h>
#include <scenefx/types/fx/corner_location.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "animations.h"
#include "decoration.h"
#include "helpers.h"
#include "keybinds.h"
#include "mwc.h"

struct output_mode_config {
    char *name;
    uint32_t width, height;
    uint32_t refresh_rate;
    double scale;
};

struct output_position_config {
    char *name;
    int32_t x, y;
};

struct workspace_config {
    char *output;
    uint32_t index;
};

struct pointer_config {
    char *name;
    double sensitivity;
    enum libinput_config_accel_profile acceleration;
};

enum blur_optimized {
    BLUR_OPTIMIZED_ALWAYS = 0,
    BLUR_OPTIMIZED_TILED_ONLY,
    BLUR_OPTIMIZED_NEVER,
};

// we usually can tell if an option is specified or not by comparing them to 0 (or NULL), but sometimes 0 can also mean
// something else. for such options we add another bool value to tell if they are specified or not. not used anymore,
// but left if needed in the future
#define WITH_SPECIFIED(type) \
    struct {                 \
        type value;          \
        bool specified;      \
    }

struct config {
    // NULL if default config
    char *dir;

    struct output_mode_config *output_modes;
    struct output_position_config *output_positions;
    struct keybind *keybinds;  // array
    struct keybind *pointer_keybinds;  // array
    struct workspace_config *workspaces;  // array
    struct {
        struct window_rule *floating;  // array
        struct window_rule_size *size;  // array
        struct window_rule_opacity *opacity;  // array
        struct window_rule *no_titlebar, *no_border, *no_shadow, *no_blur;  // array
    } window_rules;

    struct {
        struct layer_rule_blur *blur;  // array
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
    struct pointer_config *pointers;  // array
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
    uint32_t outer_gaps;
    uint32_t inner_gaps;

    uint32_t master_count;
    double master_ratio;

    // decorations
    bool client_side_decorations;

    bool borders;
    uint32_t border_width;
    uint32_t border_radius;
    enum corner_location border_radius_location;
    struct {
        struct color active, inactive;
    } border_color;

    bool shadows;
    uint32_t shadow_size;
    struct {
        int32_t x, y;
    } shadow_position;
    struct color shadow_color;
    double shadow_blur;

    bool titlebars;
    uint32_t titlebar_height;
    struct {
        struct color active, inactive;
    } titlebar_color;

    bool titlebar_include_close_button;
    uint32_t titlebar_close_button_size;
    struct {
        uint32_t left, right;
    } titlebar_close_button_padding;
    enum titlebar_close_button_shape titlebar_close_button_shape;
    enum titlebar_close_button_position titlebar_close_button_position;
    struct {
        struct color active, inactive;
    } titlebar_close_button_color;

    bool titlebar_include_title;
    bool titlebar_center_title;
    struct {
        uint32_t left, right;
    } titlebar_title_padding;
    struct color titlebar_title_color;

    // will be generated from the name specified by `titlebar_title_font`, may be NULL
    struct fcft_font *font;

    // opacity and blur
    struct {
        double active, inactive;
    } opacity;
    bool opacity_apply_when_fullscreen;
    bool blur;
    enum blur_optimized blur_optimized;
    struct blur_data blur_params;

    // animations stuff
    bool animations;
    uint32_t animation_duration;
    struct fx_animation_curve *animation_curve;

    // run on startup
    char **run;  // array
};

struct config *
config_load();

void
config_reload();

void
config_destroy(struct config *c);

void *
config_watch(void *data);
