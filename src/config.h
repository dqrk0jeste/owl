#pragma once

#include <libinput.h>
#include <regex.h>
#include <scenefx/types/fx/blur_data.h>
#include <scenefx/types/fx/corner_location.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

#include "decoration.h"
#include "helpers.h"
#include "keybinds.h"

enum output_field {
    OUTPUT_FIELD_MATCH_NAME = 1 << 0,

    OUTPUT_FIELD_MODE = 1 << 1,
    OUTPUT_FIELD_POSITION = 1 << 2,
    OUTPUT_FIELD_SCALE = 1 << 3,
    OUTPUT_FIELD_WORKSPACES = 1 << 4,
    OUTPUT_FIELD_MASTER_COUNT = 1 << 5,
    OUTPUT_FIELD_MASTER_RATIO = 1 << 6,
};

struct output_config {
    uint32_t specified;  // bitmask of `enum output_field`
    char *name;

    int width, height, refresh;
    int x, y;
    double scale;
    int *workspaces;  // array
    int master_count;
    double master_ratio;
};

enum pointer_field {
    POINTER_FIELD_MATCH_NAME = 1 << 0,

    POINTER_FIELD_SENSITIVITY = 1 << 1,
    POINTER_FIELD_ACCELERATION = 1 << 2,
    POINTER_FIELD_LEFT_HANDED = 1 << 3,
};

struct pointer_config {
    uint32_t specified;  // bitmask of `pointer_field`
    char *name;

    double sensitivity;
    enum libinput_config_accel_profile acceleration;
    bool left_handed;
};

enum keyboard_field {
    KEYBOARD_FIELD_MATCH_NAME = 1 << 0,

    KEYBOARD_FIELD_RATE = 1 << 1,
    KEYBOARD_FIELD_DELAY = 1 << 2,
    KEYBOARD_FIELD_OPTIONS = 1 << 3,
};

struct keyboard_config {
    uint32_t specified;  // bitmask of `keyboard_field`
    char *name;

    int rate, delay;
    char *options;
};

struct trackpad_config {
    bool disable_while_typing, natural_scroll, tap_to_click;
    enum libinput_config_scroll_method scroll_method;
};

struct cursor_config {
    // todo: check null values
    char *theme;
    int size;
};

enum gaps_field {
    GAPS_FIELD_MATCH_OUTPUT = 1 << 0,
    GAPS_FIELD_MATCH_MASTER_COUNT = 1 << 1,
    GAPS_FIELD_MATCH_SLAVE_COUNT = 1 << 2,

    GAPS_FIELD_OUTER = 1 << 3,
    GAPS_FIELD_INNER = 1 << 4,
};

struct gaps {
    int top, right, bottom, left;
};

struct gaps_config {
    uint32_t specified;  // bitmask of `layout_field`
    char *output;
    enum relation master_relation;
    int master_count;
    enum relation slave_relation;
    int slave_count;

    struct gaps inner, outer;
};

struct titlebar_config {
    int height;
    struct {
        struct color active, inactive;
    } color;

    struct {
        bool enabled;
        int size;
        struct {
            int left, right;
        } padding;
        enum titlebar_close_button_shape shape;
        enum titlebar_close_button_position position;
        struct {
            struct color active, inactive;
        } color;
    } close_button;

    struct {
        bool enabled;
        int size;
        enum titlebar_title_position position;
        struct {
            int left, right;
        } padding;
        struct color color;
        char *font;
    } title;
};

struct border_config {
    int width;
    struct {
        struct color active, inactive;
    } color;
};

struct shadow_config {
    int size;
    int x, y;
    struct color color;
    double blur;
};

struct animations_config {
    bool enabled;
    int duration;
    double curve[4];
};

struct blur_config {
    struct blur_data params;
};

enum toplevel_field {
    TOPLEVEL_FIELD_MATCH_APP_ID = 1 << 0,
    TOPLEVEL_FIELD_MATCH_TITLE = 1 << 1,
    TOPLEVEL_FIELD_MATCH_MODE = 1 << 2,
    TOPLEVEL_FIELD_MATCH_STATE = 1 << 3,
    TOPLEVEL_FIELD_MATCH_MASTER_COUNT = 1 << 4,
    TOPLEVEL_FIELD_MATCH_SLAVE_COUNT = 1 << 5,

    TOPLEVEL_FIELD_CORNER_RADIUS = 1 << 6,
    TOPLEVEL_FIELD_CORNER_LOCATION = 1 << 7,
    TOPLEVEL_FIELD_OPACITY = 1 << 8,
    TOPLEVEL_FIELD_APPLY_OPACITY_TO_DECORATIONS = 1 << 9,
    TOPLEVEL_FIELD_CLIENT_SIDE_DECORATIONS = 1 << 10,
    TOPLEVEL_FIELD_BLUR = 1 << 11,
    TOPLEVEL_FIELD_SHADOW = 1 << 12,
    TOPLEVEL_FIELD_BORDER = 1 << 13,
    TOPLEVEL_FIELD_TITLEBAR = 1 << 14,
    TOPLEVEL_FIELD_DEFAULT_MODE = 1 << 15,
    TOPLEVEL_FIELD_DEFAULT_SIZE = 1 << 16,
};

enum toplevel_default_mode {
    TOPLEVEL_DEFAULT_MODE_FLOATING,
    TOPLEVEL_DEFAULT_MODE_TILED,
};

enum toplevel_mode_ext {
    TOPLEVEL_MODE_EXT_MASTER = 1 << 0,
    TOPLEVEL_MODE_EXT_SLAVE = 1 << 1,
    TOPLEVEL_MODE_EXT_TILED = TOPLEVEL_MODE_EXT_MASTER | TOPLEVEL_MODE_EXT_SLAVE,
    TOPLEVEL_MODE_EXT_FLOATING = 1 << 2,
    TOPLEVEL_MODE_EXT_FULLSCREEN = 1 << 3,
    TOPLEVEL_MODE_EXT_GRABBED = 1 << 4,
};

struct toplevel_config {
    uint32_t specified;  // bitmask of `enum toplevel_field`

    // these fields will be matched on
    regex_t app_id, title;
    enum toplevel_mode_ext mode;
    enum relation master_relation;
    int master_count;
    enum relation slave_relation;
    int slave_count;
    bool focused;

    // these will be applied if matching
    int corner_radius;
    enum corner_location corner_location;
    double opacity;
    bool apply_opacity_to_decorations;
    bool client_side_decorations;
    enum blur blur;
    bool shadow, border, titlebar;
    enum toplevel_default_mode default_mode;
    int default_width, default_height;
    bool width_is_relative, height_is_relative;
};

enum layer_field {
    LAYER_FIELD_MATCH_NAMESPACE = 1 << 0,

    LAYER_FIELD_BLUR = 1 << 1,
    LAYER_FIELD_BLUR_IGNORE_TRANSPARENT = 1 << 2,
};

struct layer_config {
    uint32_t specified;  // bitmaks of `layer_field`
    regex_t namespace;

    enum blur blur;
    bool blur_ignore_transparent;
};

enum config_section {
    CONFIG_SECTION_NONE = 0,
    CONFIG_SECTION_ENV,
    CONFIG_SECTION_ON_STARTUP,
    CONFIG_SECTION_OUTPUT,
    CONFIG_SECTION_KEYBOARD,
    CONFIG_SECTION_KEYMAPS,
    CONFIG_SECTION_POINTER,
    CONFIG_SECTION_TRACKPAD,
    CONFIG_SECTION_CURSOR,
    CONFIG_SECTION_GAPS,
    CONFIG_SECTION_TITLEBAR,
    CONFIG_SECTION_TITLEBAR_CLOSE_BUTTON,
    CONFIG_SECTION_TITLEBAR_TITLE,
    CONFIG_SECTION_BORDER,
    CONFIG_SECTION_SHADOW,
    CONFIG_SECTION_ANIMATIONS,
    CONFIG_SECTION_BLUR,
    CONFIG_SECTION_KEYBINDS,
    CONFIG_SECTION_TOPLEVEL,
    CONFIG_SECTION_LAYER,
};

struct config {
    char **on_startup;  // array
    struct output_config *outputs;  // array
    struct keyboard_config *keyboards;  // array
    char *keymap_layouts, *keymap_variants;  // string
    struct pointer_config *pointers;  // array
    struct trackpad_config trackpad;
    struct cursor_config cursor;
    struct gaps_config *gaps;  // array
    struct titlebar_config titlebar;
    struct border_config border;
    struct shadow_config shadow;
    struct animations_config animations;
    struct blur_config blur;
    struct keybind *keybinds, *pointer_keybinds;  // array
    struct toplevel_config *toplevels;  // array
    struct layer_config *layers;  // array

    // a value that tells if we should or should not have blur nodes enabled. we do this to minimize the
    // load if the user does not want any blur, so that the blur is not recalculated at all.
    bool needs_optimized_blur;
};

struct config *
config_load(char *path);

void
config_destroy(struct config *c);

void
config_watcher_init(char *dir);

void
config_watcher_deinit(void);

bool
config_watcher_running(void);
