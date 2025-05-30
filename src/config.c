#include "config.h"

#include <assert.h>
#include <fcft/fcft.h>
#include <libinput.h>
#include <limits.h>
#include <regex.h>
#include <scenefx/types/fx/blur_data.h>
#include <scenefx/types/fx/corner_location.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>

#include "array.h"
#include "dyn_string.h"
#include "font.h"
#include "helpers.h"
#include "keybinds.h"
#include "layout.h"
#include "mwc.h"
#include "output.h"
#include "pointer.h"
#include "rules.h"
#include "workspace.h"
#define PARSER_IMPLEMENTATION
#include "parser.h"

extern struct server server;

// this is a helper for logging the config errors
int line_number;
#define ERROR(msg, ...) wlr_log(WLR_ERROR, "config: line %d: " msg, line_number, ##__VA_ARGS__)

// assumes there is `arg_count` defined in the scope
#define NEED_ARGUMENTS(count)                                          \
    if(arg_count < count) {                                            \
        ERROR("expected %d arguments, but got %zu", count, arg_count); \
        return;                                                        \
    }

// assumes valid hex
static int
hex_to_unsigned_decimal(char *hex, size_t len) {
    int result = 0;
    for(size_t i = 0; i < len; i++) {
        result *= 16;
        char current = hex[i];
        if(current >= '0' && current <= '9') {
            result += (current - '0');
        } else if(current >= 'a' && current <= 'f') {
            result += (current - 'a') + 10;
        } else if(current >= 'A' && current <= 'F') {
            result += (current - 'A') + 10;
        }
    }

    return result;
}

static struct color
parse_color(char *s) {
    size_t len = strlen(s);

    if(len == 6) {
        return (struct color){
                .r = clamp(hex_to_unsigned_decimal(s + 0, 2), 0, 255),
                .g = clamp(hex_to_unsigned_decimal(s + 2, 2), 0, 255),
                .b = clamp(hex_to_unsigned_decimal(s + 4, 2), 0, 255),
                .a = 255,
        };
    } else if(len == 8) {
        return (struct color){
                .r = clamp(hex_to_unsigned_decimal(s + 0, 2), 0, 255),
                .g = clamp(hex_to_unsigned_decimal(s + 2, 2), 0, 255),
                .b = clamp(hex_to_unsigned_decimal(s + 4, 2), 0, 255),
                .a = clamp(hex_to_unsigned_decimal(s + 6, 2), 0, 255),
        };
    }

    ERROR("invalid color `%s`", s);
    return (struct color){0};
}

// handle appending to the config string
static void
add_keymap(struct config *c, char *layout, char *variant) {
    if(string_len(c->keymap_layouts) != 0) {
        // this is not the first one, so we put a comma
        string_append(&c->keymap_layouts, ',');
        string_append(&c->keymap_variants, ',');
    }

    string_append_c_string(&c->keymap_layouts, layout);
    if(variant != NULL) {
        string_append_c_string(&c->keymap_variants, variant);
    }
}

static void
add_keybind(struct config *c, char *modifiers, char *key, char *action, char **args, size_t arg_count) {
    uint32_t modifiers_flag = 0;

    char *tok = strtok(modifiers, "+");
    while(tok != NULL) {
        if(strcmp(tok, "alt") == 0) {
            modifiers_flag |= WLR_MODIFIER_ALT;
        } else if(strcmp(tok, "super") == 0) {
            modifiers_flag |= WLR_MODIFIER_LOGO;
        } else if(strcmp(tok, "ctrl") == 0) {
            modifiers_flag |= WLR_MODIFIER_CTRL;
        } else if(strcmp(tok, "shift") == 0) {
            modifiers_flag |= WLR_MODIFIER_SHIFT;
        }

        tok = strtok(NULL, "+");
    }

    uint32_t key_sym = 0;
    bool pointer = false;
    if(strncmp(key, "pointer_", strlen("pointer_")) == 0) {
        pointer = true;

        key = key + 8;
        if(strcmp(key, "left_click") == 0) {
            key_sym = 272;
        } else if(strcmp(key, "right_click") == 0) {
            key_sym = 273;
        } else if(strcmp(key, "middle_click") == 0) {
            key_sym = 274;
        } else {
            key_sym = atoi(key);
        }
    } else if(strcmp(key, "return") == 0 || strcmp(key, "enter") == 0) {
        key_sym = XKB_KEY_Return;
    } else if(strcmp(key, "backspace") == 0) {
        key_sym = XKB_KEY_BackSpace;
    } else if(strcmp(key, "delete") == 0) {
        key_sym = XKB_KEY_Delete;
    } else if(strcmp(key, "escape") == 0) {
        key_sym = XKB_KEY_Escape;
    } else if(strcmp(key, "tab") == 0) {
        key_sym = XKB_KEY_Tab;
    } else if(strcmp(key, "up") == 0) {
        key_sym = XKB_KEY_Up;
    } else if(strcmp(key, "down") == 0) {
        key_sym = XKB_KEY_Down;
    } else if(strcmp(key, "left") == 0) {
        key_sym = XKB_KEY_Left;
    } else if(strcmp(key, "right") == 0) {
        key_sym = XKB_KEY_Right;
    } else {
        key_sym = xkb_keysym_from_name(key, 0);
        if(key_sym == XKB_KEY_NoSymbol) {
            ERROR("key `%s` doesn't seem right", key);
            return;
        }
    }

    struct keybind keybind = (struct keybind){
            .modifiers = modifiers_flag,
            .key = key_sym,
    };

    if(strcmp(action, "exit") == 0) {
        keybind.action = keybind_stop_server;
    } else if(strcmp(action, "run") == 0) {
        NEED_ARGUMENTS(1);

        keybind.action = keybind_run;
        keybind.data = strdup(args[0]);
    } else if(strcmp(action, "close") == 0) {
        keybind.action = keybind_close;
    } else if(strcmp(action, "toggle_floating") == 0) {
        keybind.action = keybind_toggle_floating;
    } else if(strcmp(action, "start_resize") == 0) {
        keybind.action = keybind_start_resize;
        keybind.stop = keybind_stop_resize;
    } else if(strcmp(action, "start_move") == 0) {
        keybind.action = keybind_start_move;
        keybind.stop = keybind_stop_move;
    } else if(strcmp(action, "move_focus") == 0) {
        NEED_ARGUMENTS(1);

        enum direction direction;
        if(strcmp(args[0], "up") == 0) {
            direction = DIRECTION_UP;
        } else if(strcmp(args[0], "left") == 0) {
            direction = DIRECTION_LEFT;
        } else if(strcmp(args[0], "down") == 0) {
            direction = DIRECTION_DOWN;
        } else if(strcmp(args[0], "right") == 0) {
            direction = DIRECTION_RIGHT;
        } else {
            ERROR("invalid option `%s`", args[0]);
            return;
        }

        keybind.action = keybind_move_focus;
        keybind.data = (void *)direction;
    } else if(strcmp(action, "move") == 0) {
        NEED_ARGUMENTS(1);

        enum direction direction;
        if(strcmp(args[0], "up") == 0) {
            direction = DIRECTION_UP;
        } else if(strcmp(args[0], "left") == 0) {
            direction = DIRECTION_LEFT;
        } else if(strcmp(args[0], "down") == 0) {
            direction = DIRECTION_DOWN;
        } else if(strcmp(args[0], "right") == 0) {
            direction = DIRECTION_RIGHT;
        } else {
            ERROR("invalid option `%s`", args[0]);
            return;
        }

        keybind.action = keybind_move;
        keybind.data = (void *)direction;
    } else if(strcmp(action, "workspace") == 0) {
        NEED_ARGUMENTS(1);

        keybind.action = keybind_change_workspace;
        keybind.data = (void *)(intptr_t)atoi(args[0]);
    } else if(strcmp(action, "move_to_workspace") == 0) {
        NEED_ARGUMENTS(1);

        keybind.action = keybind_move_to_workspace;
        keybind.data = (void *)(intptr_t)atoi(args[0]);
    } else if(strcmp(action, "next_workspace") == 0) {
        keybind.action = keybind_next_workspace;
    } else if(strcmp(action, "prev_workspace") == 0) {
        keybind.action = keybind_prev_workspace;
    } else if(strcmp(action, "toggle_fullscreen") == 0) {
        keybind.action = keybind_toggle_fullscreen;
    } else if(strcmp(action, "toggle_fake_fullscreen") == 0) {
        keybind.action = keybind_toggle_fake_fullscreen;
    } else if(strcmp(action, "increase_master_ratio") == 0) {
        NEED_ARGUMENTS(1);

        keybind.action = keybind_increase_master_ratio;
        // ugly hack to keep a double in this field, tho with only two digits of precision, idk
        keybind.data = (void *)(intptr_t)(atof(args[0]) * 100);
    } else if(strcmp(action, "decrease_master_ratio") == 0) {
        NEED_ARGUMENTS(1);

        keybind.action = keybind_decrease_master_ratio;
        keybind.data = (void *)(intptr_t)(atof(args[0]) * 100);
    } else {
        ERROR("invalid action `%s`", action);
        return;
    }

    if(pointer) {
        array_push(&c->pointer_keybinds, keybind);
    } else {
        array_push(&c->keybinds, keybind);
    }
}

static struct config *
create_default_config(void) {
    struct config *c = calloc(1, sizeof(*c));

    // initialize all the arrays and strings
    array_init(&c->on_startup);
    array_init(&c->outputs);
    array_init(&c->keyboards);
    array_init(&c->pointers);
    array_init(&c->keybinds);
    array_init(&c->pointer_keybinds);
    array_init(&c->gaps);
    array_init(&c->toplevels);
    array_init(&c->layers);

    c->keymap_layouts = string_new_empty();
    c->keymap_variants = string_new_empty();

    // add default options
    struct output_config default_output_config = {
            .specified = OUTPUT_FIELD_MODE | OUTPUT_FIELD_MASTER_COUNT | OUTPUT_FIELD_MASTER_RATIO |
                    OUTPUT_FIELD_SCALE | OUTPUT_FIELD_WORKSPACES,
            // we specify the mode, but this is only used as a flag to choose preffered
            .width = 0,
            .height = 0,
            .refresh = 0,
            .master_count = 1,
            .master_ratio = 0.5,
            .scale = 1.0,
    };
    array_init(&default_output_config.workspaces);
    array_push(&default_output_config.workspaces, 0);

    // add it at the beggining of the array
    array_push(&c->outputs, default_output_config);

    array_push(&c->keyboards,
            ((struct keyboard_config){
                    .specified = KEYBOARD_FIELD_RATE | KEYBOARD_FIELD_DELAY,
                    .name = NULL,
                    .rate = 150,
                    .delay = 50,
            }));

    array_push(&c->pointers,
            ((struct pointer_config){
                    .specified = POINTER_FIELD_SENSITIVITY | POINTER_FIELD_ACCELERATION | POINTER_FIELD_LEFT_HANDED,
                    .name = NULL,
                    .sensitivity = 0.0,
                    .acceleration = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT,
                    .left_handed = false,
            }));

    array_push(&c->gaps,
            ((struct gaps_config){
                    .specified = GAPS_FIELD_INNER | GAPS_FIELD_OUTER,
                    .inner = {0},
                    .outer = {0},
            }));

    array_push(&c->toplevels,
            ((struct toplevel_config){
                    .specified = TOPLEVEL_FIELD_CORNER_RADIUS | TOPLEVEL_FIELD_CORNER_LOCATION |
                            TOPLEVEL_FIELD_OPACITY | TOPLEVEL_FIELD_CLIENT_SIDE_DECORATIONS | TOPLEVEL_FIELD_BLUR |
                            TOPLEVEL_FIELD_SHADOW | TOPLEVEL_FIELD_BORDER | TOPLEVEL_FIELD_TITLEBAR |
                            TOPLEVEL_FIELD_DEFAULT_MODE | TOPLEVEL_FIELD_DEFAULT_POSITION,
                    .corner_radius = 0,
                    .corner_location = CORNER_LOCATION_ALL,
                    .opacity = 1.0,
                    .client_side_decorations = false,
                    .blur = BLUR_NONE,
                    .shadow = false,
                    .border = false,
                    .titlebar = false,
                    .default_mode = TOPLEVEL_DEFAULT_MODE_TILED,
                    .default_position = {0},
            }));

    array_push(&c->layers,
            ((struct layer_config){
                    .specified = LAYER_FIELD_BLUR | LAYER_FIELD_BLUR_IGNORE_TRANSPARENT,
                    .blur = BLUR_NONE,
                    .blur_ignore_transparent = false,
            }));

    // todo: test cursor themes and sizes and null values
    // c->cursor.size = 24;
    // also check titlebars and shadows with null values

    c->animations.duration = 500;
    c->needs_optimized_blur = false;

    return c;
}

static enum config_section
get_section(struct config *c, char *name) {
    if(strcmp(name, "env") == 0) {
        return CONFIG_SECTION_ENV;
    } else if(strcmp(name, "on_startup") == 0) {
        return CONFIG_SECTION_ON_STARTUP;
    } else if(strcmp(name, "output") == 0) {
        array_push(&c->outputs, (struct output_config){0});
        return CONFIG_SECTION_OUTPUT;
    } else if(strcmp(name, "keyboard") == 0) {
        array_push(&c->keyboards, (struct keyboard_config){0});
        return CONFIG_SECTION_KEYBOARD;
    } else if(strcmp(name, "keymaps") == 0) {
        return CONFIG_SECTION_KEYMAPS;
    } else if(strcmp(name, "pointer") == 0) {
        array_push(&c->pointers, (struct pointer_config){0});
        return CONFIG_SECTION_POINTER;
    } else if(strcmp(name, "trackpad") == 0) {
        return CONFIG_SECTION_TRACKPAD;
    } else if(strcmp(name, "cursor") == 0) {
        return CONFIG_SECTION_CURSOR;
    } else if(strcmp(name, "gaps") == 0) {
        array_push(&c->gaps, (struct gaps_config){0});
        return CONFIG_SECTION_GAPS;
    } else if(strcmp(name, "titlebar") == 0) {
        return CONFIG_SECTION_TITLEBAR;
    } else if(strcmp(name, "titlebar:close_button") == 0) {
        return CONFIG_SECTION_TITLEBAR_CLOSE_BUTTON;
    } else if(strcmp(name, "titlebar:title") == 0) {
        return CONFIG_SECTION_TITLEBAR_TITLE;
    } else if(strcmp(name, "border") == 0) {
        return CONFIG_SECTION_BORDER;
    } else if(strcmp(name, "shadow") == 0) {
        return CONFIG_SECTION_SHADOW;
    } else if(strcmp(name, "animations") == 0) {
        return CONFIG_SECTION_ANIMATIONS;
    } else if(strcmp(name, "blur") == 0) {
        return CONFIG_SECTION_BLUR;
    } else if(strcmp(name, "keybinds") == 0) {
        return CONFIG_SECTION_KEYBINDS;
    } else if(strcmp(name, "toplevel") == 0) {
        array_push(&c->toplevels, (struct toplevel_config){0});
        return CONFIG_SECTION_TOPLEVEL;
    } else if(strcmp(name, "layer") == 0) {
        array_push(&c->layers, (struct layer_config){0});
        return CONFIG_SECTION_LAYER;
    } else {
        ERROR("invalid section `%s`", name);
        return CONFIG_SECTION_NONE;
    }
}

static void
handle_value(struct config *c, char **words, enum config_section section) {
    // helper thats valid for all object-like sections keys. for array-like ones (like env), this should not be used
    size_t arg_count = array_len(words) - 1;

    if(section == CONFIG_SECTION_ENV) {
        if(array_len(words) < 2) {
            ERROR("no provided value for env `%s`", words[0]);
            return;
        }

        setenv(words[0], words[1], true);
    } else if(section == CONFIG_SECTION_ON_STARTUP) {
        array_push(&c->on_startup, strdup(words[0]));
    } else if(section == CONFIG_SECTION_OUTPUT) {
        struct output_config *output = array_last(c->outputs);

        if(strcmp(words[0], "name") == 0) {
            NEED_ARGUMENTS(1);

            output->name = strdup(words[1]);
            output->specified |= OUTPUT_FIELD_MATCH_NAME;
        } else if(strcmp(words[0], "mode") == 0) {
            NEED_ARGUMENTS(3);

            output->width = max(atoi(words[1]), 0);
            output->height = max(atoi(words[2]), 0);
            output->refresh = max(atoi(words[3]), 0) * 1000;
            output->specified |= OUTPUT_FIELD_MODE;
        } else if(strcmp(words[0], "position") == 0) {
            NEED_ARGUMENTS(2);

            output->x = max(atoi(words[1]), 0);
            output->y = max(atoi(words[2]), 0);
            output->specified |= OUTPUT_FIELD_POSITION;
        } else if(strcmp(words[0], "scale") == 0) {
            NEED_ARGUMENTS(1);

            output->scale = max(atof(words[1]), 1.0);
            output->specified |= OUTPUT_FIELD_SCALE;
        } else if(strcmp(words[0], "master_count") == 0) {
            NEED_ARGUMENTS(1);

            output->master_count = max(atoi(words[1]), 1);
            output->specified |= OUTPUT_FIELD_MASTER_COUNT;
        } else if(strcmp(words[0], "master_ratio") == 0) {
            NEED_ARGUMENTS(1);

            output->master_ratio = max(atof(words[1]), 0);
            output->specified |= OUTPUT_FIELD_MASTER_RATIO;
        } else if(strcmp(words[0], "workspaces") == 0) {
            array_init(&output->workspaces);
            for(size_t i = 1; i < array_len(words); i++) {
                array_push(&output->workspaces, atoi(words[i]));
            }
            output->specified |= OUTPUT_FIELD_WORKSPACES;
        } else {
            ERROR("unknown keyword `%s` for section `output`", words[0]);
        }
    } else if(section == CONFIG_SECTION_KEYBOARD) {
        struct keyboard_config *keyboard = array_last(c->keyboards);

        if(strcmp(words[0], "name") == 0) {
            NEED_ARGUMENTS(1);

            keyboard->name = strdup(words[1]);
            keyboard->specified |= KEYBOARD_FIELD_MATCH_NAME;
        } else if(strcmp(words[0], "rate") == 0) {
            NEED_ARGUMENTS(1);

            keyboard->rate = max(atoi(words[1]), 1);
            keyboard->specified |= KEYBOARD_FIELD_RATE;
        } else if(strcmp(words[0], "delay") == 0) {
            NEED_ARGUMENTS(1);

            keyboard->delay = max(atoi(words[1]), 1);
            keyboard->specified |= KEYBOARD_FIELD_DELAY;
        } else if(strcmp(words[0], "options") == 0) {
            NEED_ARGUMENTS(1);

            keyboard->options = strdup(words[1]);
            keyboard->specified |= KEYBOARD_FIELD_OPTIONS;
        } else {
            ERROR("unknown keyword `%s` for section `keyboard`", words[0]);
        }
    } else if(section == CONFIG_SECTION_KEYMAPS) {
        add_keymap(c, words[0], array_len(words) > 1 ? words[1] : NULL);
    } else if(section == CONFIG_SECTION_POINTER) {
        struct pointer_config *pointer = array_last(c->pointers);

        if(strcmp(words[0], "name") == 0) {
            NEED_ARGUMENTS(1);

            pointer->name = strdup(words[1]);
            pointer->specified |= POINTER_FIELD_MATCH_NAME;
        } else if(strcmp(words[0], "sensitivity") == 0) {
            NEED_ARGUMENTS(1);

            pointer->sensitivity = clamp(atof(words[1]), -1.0, 1.0);
            pointer->specified |= POINTER_FIELD_SENSITIVITY;
        } else if(strcmp(words[0], "acceleration") == 0) {
            NEED_ARGUMENTS(1);

            pointer->acceleration =
                    atoi(words[1]) ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
            pointer->specified |= POINTER_FIELD_ACCELERATION;
        } else if(strcmp(words[0], "left_handed") == 0) {
            NEED_ARGUMENTS(1);

            pointer->left_handed = atoi(words[1]);
            pointer->specified |= POINTER_FIELD_LEFT_HANDED;
        } else {
            ERROR("unknown keyword `%s` for section `pointer`", words[0]);
        }
    } else if(section == CONFIG_SECTION_TRACKPAD) {
        if(strcmp(words[0], "disable_while_typing") == 0) {
            NEED_ARGUMENTS(1);

            c->trackpad.disable_while_typing = atoi(words[1]);
        } else if(strcmp(words[0], "natural_scroll") == 0) {
            NEED_ARGUMENTS(1);

            c->trackpad.natural_scroll = atoi(words[1]);
        } else if(strcmp(words[0], "tap_to_click") == 0) {
            NEED_ARGUMENTS(1);

            c->trackpad.tap_to_click = atoi(words[1]);
        } else if(strcmp(words[0], "scroll_method") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "no_scroll") == 0) {
                c->trackpad.scroll_method = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
            } else if(strcmp(words[1], "two_fingers") == 0) {
                c->trackpad.scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
            } else if(strcmp(words[1], "edge") == 0) {
                c->trackpad.scroll_method = LIBINPUT_CONFIG_SCROLL_EDGE;
            } else if(strcmp(words[1], "on_button_down") == 0) {
                c->trackpad.scroll_method = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
            } else {
                ERROR("invalid option `%s`", words[1]);
            }
        } else {
            ERROR("unknown keyword `%s` for section `trackpad`", words[0]);
        }
    } else if(section == CONFIG_SECTION_CURSOR) {
        if(strcmp(words[0], "theme") == 0) {
            NEED_ARGUMENTS(1);

            c->cursor.theme = strdup(words[1]);
        } else if(strcmp(words[0], "size") == 0) {
            NEED_ARGUMENTS(1);

            c->cursor.size = max(atoi(words[1]), 0);
        } else if(strcmp(words[0], "warp") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "never") == 0) {
                c->cursor.warp = CURSOR_WARP_NEVER;
            } else if(strcmp(words[1], "on_output_change") == 0) {
                c->cursor.warp = CURSOR_WARP_ON_OUTPUT_CHANGE;
            } else if(strcmp(words[1], "always") == 0) {
                c->cursor.warp = CURSOR_WARP_ALWAYS;
            } else {
                ERROR("invalid option `%s`", words[1]);
            }
        } else if(strcmp(words[0], "hide_after") == 0) {
            NEED_ARGUMENTS(1);

            c->cursor.hide_after = max(atoi(words[1]), 0);
        } else {
            ERROR("unknown keyword `%s` for section `cursor`", words[0]);
        }
    } else if(section == CONFIG_SECTION_GAPS) {
        struct gaps_config *gaps = array_last(c->gaps);
        if(strcmp(words[0], "output") == 0) {
            NEED_ARGUMENTS(1);

            gaps->output = strdup(words[1]);
            gaps->specified |= GAPS_FIELD_MATCH_OUTPUT;
        } else if(strcmp(words[0], "master_count") == 0) {
            NEED_ARGUMENTS(2);

            if(strcmp(words[1], "<") == 0) {
                gaps->master_relation = RELATION_SMALLER_THAN;
            } else if(strcmp(words[1], ">") == 0) {
                gaps->master_relation = RELATION_GREATER_THAN;
            } else if(strcmp(words[1], "=") == 0) {
                gaps->master_relation = RELATION_EQUAL;
            } else {
                ERROR("invalid relation `%s`", words[1]);
                return;
            }

            gaps->master_count = max(atoi(words[2]), 0);
            gaps->specified |= GAPS_FIELD_MATCH_MASTER_COUNT;
        } else if(strcmp(words[0], "slave_count") == 0) {
            NEED_ARGUMENTS(2);

            if(strcmp(words[1], "<") == 0) {
                gaps->slave_relation = RELATION_SMALLER_THAN;
            } else if(strcmp(words[1], ">") == 0) {
                gaps->slave_relation = RELATION_GREATER_THAN;
            } else if(strcmp(words[1], "=") == 0) {
                gaps->slave_relation = RELATION_EQUAL;
            } else {
                ERROR("invalid relation `%s`", words[1]);
                return;
            }

            gaps->slave_count = max(atoi(words[2]), 0);
            gaps->specified |= GAPS_FIELD_MATCH_SLAVE_COUNT;
        } else if(strcmp(words[0], "outer") == 0) {
            if(arg_count == 1) {
                gaps->outer.top = gaps->outer.right = gaps->outer.bottom = gaps->outer.left = max(atoi(words[1]), 0);
            } else if(arg_count == 2) {
                gaps->outer.top = gaps->outer.bottom = max(atoi(words[1]), 0);
                gaps->outer.left = gaps->outer.right = max(atoi(words[2]), 0);
            } else if(arg_count == 4) {
                gaps->outer.top = max(atoi(words[1]), 0);
                gaps->outer.right = max(atoi(words[2]), 0);
                gaps->outer.bottom = max(atoi(words[3]), 0);
                gaps->outer.left = max(atoi(words[4]), 0);
            } else {
                ERROR("expected 1, 2 or 4 arguments, but got %zu", arg_count);
                return;
            }

            gaps->specified |= GAPS_FIELD_OUTER;
        } else if(strcmp(words[0], "inner") == 0) {
            if(arg_count == 1) {
                gaps->inner.top = gaps->inner.right = gaps->inner.bottom = gaps->inner.left = max(atoi(words[1]), 0);
            } else if(arg_count == 2) {
                gaps->inner.top = gaps->inner.bottom = max(atoi(words[1]), 0);
                gaps->inner.left = gaps->inner.right = max(atoi(words[2]), 0);
            } else if(arg_count == 4) {
                gaps->inner.top = max(atoi(words[1]), 0);
                gaps->inner.right = max(atoi(words[2]), 0);
                gaps->inner.bottom = max(atoi(words[3]), 0);
                gaps->inner.left = max(atoi(words[4]), 0);
            } else {
                ERROR("expected 1, 2 or 4 arguments, but got %zu", arg_count);
                return;
            }

            gaps->specified |= GAPS_FIELD_INNER;
        } else {
            ERROR("unknown keyword `%s` for section `gaps`", words[0]);
            return;
        }
    } else if(section == CONFIG_SECTION_TITLEBAR) {
        if(strcmp(words[0], "height") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.height = max(atoi(words[1]), 0);
        } else if(strcmp(words[0], "color") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.color.active = parse_color(words[1]);
            c->titlebar.color.inactive = arg_count > 1 ? parse_color(words[2]) : c->titlebar.color.active;
        } else {
            ERROR("unknown keyword `%s` for section `titlebar`", words[0]);
        }
    } else if(section == CONFIG_SECTION_TITLEBAR_CLOSE_BUTTON) {
        if(strcmp(words[0], "enable") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.close_button.enabled = atoi(words[1]);
        } else if(strcmp(words[0], "size") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.close_button.size = max(atoi(words[1]), 0);
        } else if(strcmp(words[0], "position") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "left") == 0) {
                c->titlebar.close_button.position = TITLEBAR_CLOSE_BUTTON_POSITION_LEFT;
            } else if(strcmp(words[1], "right") == 0) {
                c->titlebar.close_button.position = TITLEBAR_CLOSE_BUTTON_POSITION_RIGHT;
            } else {
                ERROR("invalid option `%s`", words[1]);
            }
        } else if(strcmp(words[0], "padding") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.close_button.padding.left = atoi(words[1]);
            c->titlebar.close_button.padding.right =
                    arg_count > 1 ? atoi(words[1]) : c->titlebar.close_button.padding.left;
        } else if(strcmp(words[0], "shape") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "square") == 0) {
                c->titlebar.close_button.shape = TITLEBAR_CLOSE_BUTTON_SHAPE_SQUARE;
            } else if(strcmp(words[1], "circle") == 0) {
                c->titlebar.close_button.shape = TITLEBAR_CLOSE_BUTTON_SHAPE_CIRCLE;
            } else {
                ERROR("invalid option `%s`", words[1]);
            }
        } else if(strcmp(words[0], "color") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.close_button.color.active = parse_color(words[1]);
            c->titlebar.close_button.color.inactive =
                    arg_count > 1 ? parse_color(words[2]) : c->titlebar.close_button.color.active;
        } else {
            ERROR("unknown keyword `%s` for section `titlebar:close_button`", words[0]);
        }
    } else if(section == CONFIG_SECTION_TITLEBAR_TITLE) {
        if(strcmp(words[0], "enable") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.title.enabled = atoi(words[1]);
        } else if(strcmp(words[0], "size") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.title.size = max(atoi(words[1]), 0);
        } else if(strcmp(words[0], "position") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "left") == 0) {
                c->titlebar.title.position = TITLEBAR_TITLE_POSITION_LEFT;
            } else if(strcmp(words[1], "center") == 0) {
                c->titlebar.title.position = TITLEBAR_TITLE_POSITION_CENTER;
            } else {
                ERROR("invalid option `%s`", words[1]);
            }
        } else if(strcmp(words[0], "padding") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.title.padding.left = atoi(words[1]);
            c->titlebar.title.padding.right = arg_count > 1 ? atoi(words[2]) : c->titlebar.title.padding.left;
        } else if(strcmp(words[0], "color") == 0) {
            NEED_ARGUMENTS(1);

            c->titlebar.title.color = parse_color(words[1]);
        } else if(strcmp(words[0], "font") == 0) {
            NEED_ARGUMENTS(1);

            array_init(&c->titlebar.title.fonts);
            for(int i = 1; i < array_len(words); i++) {
                array_push(&c->titlebar.title.fonts, strdup(words[i]));
            }
        } else {
            ERROR("unknown keyword `%s` for section `titlebar:title`", words[0]);
        }
    } else if(section == CONFIG_SECTION_BORDER) {
        if(strcmp(words[0], "width") == 0) {
            NEED_ARGUMENTS(1);

            c->border.width = max(atoi(words[1]), 0);
        } else if(strcmp(words[0], "color") == 0) {
            NEED_ARGUMENTS(1);

            c->border.color.active = parse_color(words[1]);
            c->border.color.inactive = arg_count > 1 ? parse_color(words[2]) : c->border.color.active;
        } else {
            ERROR("unknown keyword `%s` for section `border`", words[0]);
        }
    } else if(section == CONFIG_SECTION_SHADOW) {
        if(strcmp(words[0], "blur") == 0) {
            NEED_ARGUMENTS(1);

            c->shadow.blur = max(atoi(words[1]), 0);
        } else if(strcmp(words[0], "size") == 0) {
            NEED_ARGUMENTS(1);

            c->shadow.size = max(atoi(words[1]), 0);
        } else if(strcmp(words[0], "position") == 0) {
            NEED_ARGUMENTS(2);

            c->shadow.x = atoi(words[1]);
            c->shadow.y = atoi(words[2]);
        } else if(strcmp(words[0], "color") == 0) {
            NEED_ARGUMENTS(1);

            c->shadow.color.active = parse_color(words[1]);
            c->shadow.color.inactive = arg_count > 1 ? parse_color(words[2]) : c->shadow.color.active;
        } else {
            ERROR("unknown keyword `%s` for section `shadow`", words[0]);
        }
    } else if(section == CONFIG_SECTION_ANIMATIONS) {
        if(strcmp(words[0], "enable") == 0) {
            NEED_ARGUMENTS(1);

            c->animations.enabled = atoi(words[1]);
        } else if(strcmp(words[0], "duration") == 0) {
            NEED_ARGUMENTS(1);

            c->animations.duration = max(atoi(words[1]), 1);
        } else if(strcmp(words[0], "curve") == 0) {
            NEED_ARGUMENTS(4);

            c->animations.curve[0] = atof(words[1]);
            c->animations.curve[1] = atof(words[2]);
            c->animations.curve[2] = atof(words[3]);
            c->animations.curve[3] = atof(words[4]);
        } else {
            ERROR("unknown keyword `%s` for section `animations`", words[0]);
        }
    } else if(section == CONFIG_SECTION_BLUR) {
        if(strcmp(words[0], "passes") == 0) {
            NEED_ARGUMENTS(1);

            c->blur.params.num_passes = max(atoi(words[1]), 1);
        } else if(strcmp(words[0], "size") == 0) {
            NEED_ARGUMENTS(1);

            // todo: investigate 0
            c->blur.params.radius = max(atoi(words[1]), 0);
        } else if(strcmp(words[0], "noise") == 0) {
            NEED_ARGUMENTS(1);

            c->blur.params.noise = max(atof(words[1]), 0.0);
        } else if(strcmp(words[0], "brightness") == 0) {
            NEED_ARGUMENTS(1);

            c->blur.params.brightness = max(atof(words[1]), 0.0);
        } else if(strcmp(words[0], "contrast") == 0) {
            NEED_ARGUMENTS(1);

            c->blur.params.contrast = max(atof(words[1]), 0.0);
        } else if(strcmp(words[0], "saturation") == 0) {
            NEED_ARGUMENTS(1);

            c->blur.params.saturation = max(atof(words[1]), 0.0);
        } else {
            ERROR("unknown keyword `%s` for section `blur`", words[0]);
        }
    } else if(section == CONFIG_SECTION_KEYBINDS) {
        if(array_len(words) < 3) {
            ERROR("invalid keybind format");
            return;
        }

        add_keybind(c, words[0], words[1], words[2], &words[3], array_len(words) - 3);
    } else if(section == CONFIG_SECTION_TOPLEVEL) {
        struct toplevel_config *toplevel = array_last(c->toplevels);

        if(strcmp(words[0], "app_id") == 0) {
            NEED_ARGUMENTS(1);

            regex_t regex;
            if(regcomp(&regex, words[1], REG_EXTENDED) != 0) {
                ERROR("`%s` is not a valid regex", words[1]);
                return;
            }

            toplevel->app_id = regex;
            toplevel->specified |= TOPLEVEL_FIELD_MATCH_APP_ID;
        } else if(strcmp(words[0], "title") == 0) {
            NEED_ARGUMENTS(1);

            regex_t regex;
            if(regcomp(&regex, words[1], REG_EXTENDED) != 0) {
                ERROR("`%s` is not a valid regex", words[1]);
                return;
            }

            toplevel->title = regex;
            toplevel->specified |= TOPLEVEL_FIELD_MATCH_TITLE;
        } else if(strcmp(words[0], "mode") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "floating") == 0) {
                toplevel->mode = TOPLEVEL_MODE_EXT_FLOATING;
            } else if(strcmp(words[1], "tiled") == 0) {
                toplevel->mode = TOPLEVEL_MODE_EXT_MASTER | TOPLEVEL_MODE_EXT_SLAVE;
            } else if(strcmp(words[1], "master") == 0) {
                toplevel->mode = TOPLEVEL_MODE_EXT_MASTER;
            } else if(strcmp(words[1], "slave") == 0) {
                toplevel->mode = TOPLEVEL_MODE_EXT_SLAVE;
            } else if(strcmp(words[1], "fullscreen") == 0) {
                toplevel->mode = TOPLEVEL_MODE_EXT_FULLSCREEN;
            } else if(strcmp(words[1], "moving") == 0) {
                toplevel->mode = TOPLEVEL_MODE_EXT_MOVING;
            } else if(strcmp(words[1], "resizing") == 0) {
                toplevel->mode = TOPLEVEL_MODE_EXT_RESIZING;
            } else if(strcmp(words[1], "grabbed") == 0) {
                toplevel->mode = TOPLEVEL_MODE_EXT_MOVING | TOPLEVEL_MODE_EXT_RESIZING;
            } else {
                ERROR("invalid option `%s`", words[1]);
                return;
            }

            toplevel->specified |= TOPLEVEL_FIELD_MATCH_MODE;
        } else if(strcmp(words[0], "is_focused") == 0) {
            NEED_ARGUMENTS(1);

            toplevel->is_focused = atoi(words[1]);
            toplevel->specified |= TOPLEVEL_FIELD_MATCH_FOCUSED;
        } else if(strcmp(words[0], "is_fake_fullscreen") == 0) {
            NEED_ARGUMENTS(1);

            toplevel->is_fake_fullscreen = atoi(words[1]);
            toplevel->specified |= TOPLEVEL_FIELD_MATCH_FAKE_FULLSCREEN;
        } else if(strcmp(words[0], "master_count") == 0) {
            NEED_ARGUMENTS(2);

            if(strcmp(words[1], "<") == 0) {
                toplevel->master_relation = RELATION_SMALLER_THAN;
            } else if(strcmp(words[1], ">") == 0) {
                toplevel->master_relation = RELATION_GREATER_THAN;
            } else if(strcmp(words[1], "=") == 0) {
                toplevel->master_relation = RELATION_EQUAL;
            } else {
                ERROR("invalid relation `%s`", words[1]);
                return;
            }

            toplevel->master_count = max(atoi(words[2]), 0);
            toplevel->specified |= TOPLEVEL_FIELD_MATCH_MASTER_COUNT;
        } else if(strcmp(words[0], "slave_count") == 0) {
            NEED_ARGUMENTS(2);

            if(strcmp(words[1], "<") == 0) {
                toplevel->slave_relation = RELATION_SMALLER_THAN;
            } else if(strcmp(words[1], ">") == 0) {
                toplevel->slave_relation = RELATION_GREATER_THAN;
            } else if(strcmp(words[1], "=") == 0) {
                toplevel->slave_relation = RELATION_EQUAL;
            } else {
                ERROR("invalid relation `%s`", words[1]);
                return;
            }

            toplevel->slave_count = max(atoi(words[2]), 0);
            toplevel->specified |= TOPLEVEL_FIELD_MATCH_SLAVE_COUNT;
        } else if(strcmp(words[0], "client_side_decorations") == 0) {
            NEED_ARGUMENTS(1);

            toplevel->client_side_decorations = atoi(words[1]);
            toplevel->specified |= TOPLEVEL_FIELD_CLIENT_SIDE_DECORATIONS;
        } else if(strcmp(words[0], "corner_radius") == 0) {
            NEED_ARGUMENTS(1);

            toplevel->corner_radius = max(atoi(words[1]), 0);
            toplevel->specified |= TOPLEVEL_FIELD_CORNER_RADIUS;
        } else if(strcmp(words[0], "corner_location") == 0) {
            NEED_ARGUMENTS(1);

            for(size_t i = 1; i < array_len(words); i++) {
                if(strcmp(words[i], "all") == 0) {
                    toplevel->corner_location |= CORNER_LOCATION_ALL;
                } else if(strcmp(words[i], "top") == 0) {
                    toplevel->corner_location |= CORNER_LOCATION_TOP;
                } else if(strcmp(words[i], "bottom") == 0) {
                    toplevel->corner_location |= CORNER_LOCATION_BOTTOM;
                } else if(strcmp(words[i], "right") == 0) {
                    toplevel->corner_location |= CORNER_LOCATION_RIGHT;
                } else if(strcmp(words[i], "left") == 0) {
                    toplevel->corner_location |= CORNER_LOCATION_LEFT;
                } else if(strcmp(words[i], "top_right") == 0) {
                    toplevel->corner_location |= CORNER_LOCATION_TOP_RIGHT;
                } else if(strcmp(words[i], "bottom_right") == 0) {
                    toplevel->corner_location |= CORNER_LOCATION_BOTTOM_RIGHT;
                } else if(strcmp(words[i], "bottom_left") == 0) {
                    toplevel->corner_location |= CORNER_LOCATION_BOTTOM_LEFT;
                } else if(strcmp(words[i], "top_left") == 0) {
                    toplevel->corner_location |= CORNER_LOCATION_TOP_LEFT;
                } else {
                    ERROR("invalid option `%s`", words[i]);
                    return;
                }
            }

            toplevel->specified |= TOPLEVEL_FIELD_CORNER_RADIUS;
        } else if(strcmp(words[0], "opacity") == 0) {
            NEED_ARGUMENTS(1);

            toplevel->opacity = max(atof(words[1]), 0);
            toplevel->specified |= TOPLEVEL_FIELD_OPACITY;
        } else if(strcmp(words[0], "apply_opacity_to_decorations") == 0) {
            NEED_ARGUMENTS(1);

            toplevel->apply_opacity_to_decorations = atoi(words[1]);
            toplevel->specified |= TOPLEVEL_FIELD_APPLY_OPACITY_TO_DECORATIONS;
        } else if(strcmp(words[0], "blur") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "none") == 0) {
                toplevel->blur = BLUR_NONE;
            } else if(strcmp(words[1], "normal") == 0) {
                toplevel->blur = BLUR_NORMAL;
            } else if(strcmp(words[1], "optimized") == 0) {
                toplevel->blur = BLUR_OPTIMIZED;
                c->needs_optimized_blur = true;
            } else {
                ERROR("invalid option `%s`", words[1]);
                return;
            }

            toplevel->specified |= TOPLEVEL_FIELD_BLUR;
        } else if(strcmp(words[0], "shadow") == 0) {
            NEED_ARGUMENTS(1);

            toplevel->shadow = atoi(words[1]);
            toplevel->specified |= TOPLEVEL_FIELD_SHADOW;
        } else if(strcmp(words[0], "border") == 0) {
            NEED_ARGUMENTS(1);

            toplevel->border = atoi(words[1]);
            toplevel->specified |= TOPLEVEL_FIELD_BORDER;
        } else if(strcmp(words[0], "titlebar") == 0) {
            NEED_ARGUMENTS(1);

            toplevel->titlebar = atoi(words[1]);
            toplevel->specified |= TOPLEVEL_FIELD_TITLEBAR;
        } else if(strcmp(words[0], "default_mode") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "tiled") == 0) {
                toplevel->default_mode = TOPLEVEL_DEFAULT_MODE_TILED;
            } else if(strcmp(words[1], "floating") == 0) {
                toplevel->default_mode = TOPLEVEL_DEFAULT_MODE_FLOATING;
            } else {
                ERROR("invalid mode `%s`", words[1]);
                return;
            }

            toplevel->specified |= TOPLEVEL_FIELD_DEFAULT_MODE;
        } else if(strcmp(words[0], "default_size") == 0) {
            NEED_ARGUMENTS(2);

            // if it ends with '%' we treat it as a relative unit
            if(words[1][strlen(words[1]) - 1] == '%') {
                words[1][strlen(words[1]) - 1] = 0;
                toplevel->width_is_relative = true;
            }
            if(words[2][strlen(words[2]) - 1] == '%') {
                words[2][strlen(words[2]) - 1] = 0;
                toplevel->height_is_relative = true;
            }

            toplevel->default_width = max(atoi(words[1]), 0);
            toplevel->default_height = max(atoi(words[2]), 0);
            toplevel->specified |= TOPLEVEL_FIELD_DEFAULT_SIZE;
        } else if(strcmp(words[0], "default_position") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "center") == 0) {
                toplevel->default_position.anchor = ANCHOR_CENTER;
            } else if(strcmp(words[1], "top_left") == 0) {
                toplevel->default_position.anchor = ANCHOR_TOP_LEFT;
            } else if(strcmp(words[1], "top_right") == 0) {
                toplevel->default_position.anchor = ANCHOR_TOP_RIGHT;
            } else if(strcmp(words[1], "bottom_right") == 0) {
                toplevel->default_position.anchor = ANCHOR_BOTTOM_RIGHT;
            } else if(strcmp(words[1], "bottom_left") == 0) {
                toplevel->default_position.anchor = ANCHOR_BOTTOM_LEFT;
            } else {
                ERROR("invalid anchor `%s`", words[1]);
                return;
            }

            toplevel->default_position.x = arg_count > 1 ? max(atoi(words[2]), 0) : 0;
            toplevel->default_position.y = arg_count > 2 ? max(atoi(words[3]), 0) : 0;
            toplevel->specified |= TOPLEVEL_FIELD_DEFAULT_POSITION;
        } else {
            ERROR("unknown keyword `%s` for section `toplevel`", words[0]);
        }
    } else if(section == CONFIG_SECTION_LAYER) {
        struct layer_config *layer = array_last(c->layers);

        if(strcmp(words[0], "namespace") == 0) {
            NEED_ARGUMENTS(1);

            regex_t regex;
            if(regcomp(&regex, words[1], REG_EXTENDED) != 0) {
                ERROR("`%s` is not a valid regex", words[1]);
                return;
            }

            layer->namespace = regex;
            layer->specified |= LAYER_FIELD_MATCH_NAMESPACE;
        } else if(strcmp(words[0], "blur") == 0) {
            NEED_ARGUMENTS(1);

            if(strcmp(words[1], "none") == 0) {
                layer->blur = BLUR_NONE;
            } else if(strcmp(words[1], "normal") == 0) {
                layer->blur = BLUR_NORMAL;
            } else if(strcmp(words[1], "optimized") == 0) {
                layer->blur = BLUR_OPTIMIZED;
                c->needs_optimized_blur = true;
            } else {
                ERROR("invalid option `%s`", words[1]);
                return;
            }

            layer->specified |= LAYER_FIELD_BLUR;
        } else if(strcmp(words[0], "blur_ignore_transparent") == 0) {
            NEED_ARGUMENTS(1);

            layer->blur_ignore_transparent = atoi(words[1]);
            layer->specified |= LAYER_FIELD_BLUR_IGNORE_TRANSPARENT;
        } else {
            ERROR("unknown keyword `%s` for section `layer`", words[0]);
        }
    }
}

struct config *
config_load(char *path) {
    FILE *config_file = fopen(path, "r");
    if(config_file == NULL)
        return NULL;

    struct config *c = create_default_config();

    line_number = 1;
    enum config_section section = CONFIG_SECTION_NONE;
    char buffer[1024];

    struct parser_line line = parser_next_line(config_file, buffer, sizeof(buffer));
    while(line.type != PARSER_LINE_TYPE_EOF) {
        if(line.type == PARSER_LINE_TYPE_SECTION) {
            section = get_section(c, line.section);
        } else if(line.type == PARSER_LINE_TYPE_WORDS && section != CONFIG_SECTION_NONE) {
            handle_value(c, line.words, section);
            array_destroy(line.words);
        }

        line = parser_next_line(config_file, buffer, sizeof(buffer));
        line_number++;
    }

    fclose(config_file);

    return c;
}

void
config_destroy(struct config *c) {
    for(char **iter = c->on_startup; iter <= array_last(c->on_startup); iter++) {
        free(*iter);
    }
    array_destroy(c->on_startup);

    for(struct output_config *iter = c->outputs; iter <= array_last(c->outputs); iter++) {
        if(iter->specified & OUTPUT_FIELD_MATCH_NAME) {
            free(iter->name);
        }
        if(iter->specified & OUTPUT_FIELD_WORKSPACES) {
            array_destroy(iter->workspaces);
        }
    }
    array_destroy(c->outputs);

    for(struct keyboard_config *iter = c->keyboards; iter <= array_last(c->keyboards); iter++) {
        if(iter->specified & KEYBOARD_FIELD_MATCH_NAME) {
            free(iter->name);
        }
        if(iter->specified & KEYBOARD_FIELD_OPTIONS) {
            free(iter->options);
        }
    }
    array_destroy(c->keyboards);

    string_destroy(c->keymap_layouts);
    string_destroy(c->keymap_variants);

    for(struct pointer_config *iter = c->pointers; iter <= array_last(c->pointers); iter++) {
        if(iter->specified & POINTER_FIELD_MATCH_NAME) {
            free(iter->name);
        }
    }
    array_destroy(c->pointers);

    if(c->cursor.theme != NULL) {
        free(c->cursor.theme);
    }

    for(struct gaps_config *iter = c->gaps; iter <= array_last(c->gaps); iter++) {
        if(iter->specified & GAPS_FIELD_MATCH_OUTPUT) {
            free(iter->output);
        }
    }
    array_destroy(c->gaps);

    if(c->titlebar.title.fonts != NULL) {
        for(int i = 0; i < array_len(c->titlebar.title.fonts); i++) {
            free(c->titlebar.title.fonts[i]);
        }

        array_destroy(c->titlebar.title.fonts);
    }

    for(struct keybind *iter = c->keybinds; iter <= array_last(c->keybinds); iter++) {
        if(iter->action == keybind_run) {
            free(iter->data);
        }
    }
    array_destroy(c->keybinds);
    array_destroy(c->pointer_keybinds);

    for(struct toplevel_config *iter = c->toplevels; iter <= array_last(c->toplevels); iter++) {
        if(iter->specified & TOPLEVEL_FIELD_MATCH_APP_ID) {
            regfree(&iter->app_id);
        }
        if(iter->specified & TOPLEVEL_FIELD_MATCH_TITLE) {
            regfree(&iter->title);
        }
    }
    array_destroy(c->toplevels);

    for(struct layer_config *iter = c->layers; iter <= array_last(c->layers); iter++) {
        if(iter->specified & LAYER_FIELD_MATCH_NAMESPACE) {
            regfree(&iter->namespace);
        }
    }
    array_destroy(c->layers);

    free(c);
}

static void
layout_reorganize(struct workspace *workspace) {
    if(workspace->master_count > workspace->output->master_count) {
        while(workspace->master_count > workspace->output->master_count) {
            demote_last_master(workspace);
        }
    } else {
        while(workspace->master_count < workspace->output->master_count && workspace->slave_count > 0) {
            promote_last_slave(workspace);
        }
    }
}

static bool
names_changed(int count, char *names[static count]) {
    if(count != server.title_font->names_count)
        return true;

    for(int i = 0; i < count; i++) {
        if(strcmp(names[i], server.title_font->names[i]) != 0)
            return true;
    }

    return false;
}

static void
config_reload(void) {
    struct config *c = config_load(server.config_path);
    if(c == NULL) {
        // if we couldnt load the config then skip the reload
        wlr_log(WLR_ERROR, "config: could not reload the config, keeping the old one");
        return;
    }

    if(server.mode == SERVER_MODE_MOVING || server.mode == SERVER_MODE_RESIZING ||
            server.mode == SERVER_MODE_RESIZING_MASTER_RATIO) {
        // stop resizing before reloading becuase the toplevel might go into abyss if the scene coords are changed
        cursor_stop_move_resize();
    }

    // we destroy the old config and set the new one
    config_destroy(server.config);
    server.config = c;

    // set the new blur parametars
    wlr_scene_set_blur_data(server.scene, c->blur.params);

    if(server.animation_curve != NULL) {
        fx_animation_curve_destroy(server.animation_curve);
    }
    server.animation_curve = fx_animation_curve_create(c->animations.curve);

    // we need to keep this font alive at least until the call to `output_configure()`, because text_node callback
    // depends on it. it is unfortunate, but there does not seem to be a better way to do it. refcounted pointer may
    // be better suited here, but i dont have the implementation of it right know, so it will have to wait. TODO:
    // this
    struct font *old_font = server.title_font;
    bool old_font_needs_destroy = false;

    if(c->titlebar.title.fonts != NULL) {
        if(server.title_font == NULL) {
            server.title_font =
                    font_create(array_len(c->titlebar.title.fonts), c->titlebar.title.fonts, c->titlebar.title.size);
        } else if(server.title_font->size != c->titlebar.title.size ||
                names_changed(array_len(c->titlebar.title.fonts), c->titlebar.title.fonts)) {
            server.title_font =
                    font_create(array_len(c->titlebar.title.fonts), c->titlebar.title.fonts, c->titlebar.title.size);
            old_font_needs_destroy = true;
        }
    }

    // we reconfigure the outputs
    struct output *iter_output;
    wl_list_for_each(iter_output, &server.outputs, link) {
        // before configuring the output we save its original usable area, so the floating toplevels can later be
        // placed at the same place on the output
        struct wlr_box old_usable_area = iter_output->usable_area;
        output_configure(iter_output, false);

        // configure the layers; this needs to happen before configuring the toplevels, since it changes the usable
        // area
        layer_surfaces_configure(iter_output);
        // recheck layer rules
        struct layer_surface *iter_layer_surface;
        for(size_t i = 0; i < 4; i++) {
            wl_list_for_each(iter_layer_surface, &(&iter_output->layers.background)[i], link) {
                rules_update_for_layer_surface(iter_layer_surface);
            }
        }

        struct workspace *iter_workspace;
        wl_list_for_each(iter_workspace, &iter_output->workspaces, link) {
            struct toplevel *iter_toplevel;
            wl_list_for_each(iter_toplevel, &iter_workspace->masters, link) {
                decoration_recreate(&iter_toplevel->decoration);
            }
            wl_list_for_each(iter_toplevel, &iter_workspace->slaves, link) {
                decoration_recreate(&iter_toplevel->decoration);
            }
            wl_list_for_each(iter_toplevel, &iter_workspace->floating, link) {
                decoration_recreate(&iter_toplevel->decoration);

                rules_update_for_toplevel(iter_toplevel);
                struct wlr_box box = iter_toplevel->deco_box;
                get_same_relative_coords(&box.x, &box.y, &old_usable_area, &iter_output->usable_area);
                toplevel_set_state(iter_toplevel, box);
            }

            if(iter_workspace->fullscreen != NULL) {
                decoration_recreate(&iter_workspace->fullscreen->decoration);
                rules_update_for_toplevel(iter_workspace->fullscreen);

                struct wlr_box output_box;
                wlr_output_layout_get_box(server.output_layout, iter_output->wlr_output, &output_box);
                toplevel_set_state(iter_workspace->fullscreen, output_box);
            }

            // master_count might have changed in the new config, so we update the layout
            layout_reorganize(iter_workspace);
            // and than send the configures to all the tiled toplevels
            layout_configure(iter_workspace);
        }
    }

    if(server.mode == SERVER_MODE_LOCKED) {
        // configure the lock screens if any
        struct lock_surface *iter;
        wl_list_for_each(iter, &server.lock_manager.current_lock->surfaces, link) {
            struct output *output = iter->wlr_lock_surface->output->data;

            struct wlr_box output_box;
            wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

            wlr_scene_node_set_position(&iter->scene_tree->node, output_box.x, output_box.y);
            wlr_session_lock_surface_v1_configure(iter->wlr_lock_surface, output_box.width, output_box.height);
        }
    }

    struct keyboard *keyboard;
    wl_list_for_each(keyboard, &server.keyboards, link) {
        keyboard_configure(keyboard);
    }

    struct pointer *pointer;
    wl_list_for_each(pointer, &server.pointers, link) {
        pointer_configure(pointer);
    }

    cursor_set_theme(c->cursor.theme, c->cursor.size);
    if(old_font_needs_destroy) {
        font_destroy(old_font);
    }
}

static int
watch_callback(int fd, uint32_t mask, void *data) {
    if((mask & WL_EVENT_ERROR) || (mask & WL_EVENT_HANGUP)) {
        wlr_log(WLR_ERROR, "config watcher: error occurred, quitting");
        config_watcher_deinit();
        return 0;
    }

    char buffer[1024];
    ssize_t len = read(fd, buffer, sizeof(buffer));
    if(len < 0) {
        wlr_log(WLR_ERROR, "config watcher: inotify failed read");
        return 0;
    }

    for(char *ptr = buffer; ptr < buffer + len;
            ptr += sizeof(struct inotify_event) + ((struct inotify_event *)ptr)->len) {
        struct inotify_event *event = (struct inotify_event *)ptr;
        if(event->mask & IN_MODIFY) {
            wlr_log(WLR_INFO, "config watcher: config modified, reloading");
            config_reload();
        }
    }

    return 0;
}

void
config_watcher_init(char *dir) {
    server.config_watcher.fd = inotify_init();
    if(server.config_watcher.fd < 0) {
        wlr_log(WLR_ERROR, "config watcher: failed to start");
        return;
    }

    server.config_watcher.wd = inotify_add_watch(server.config_watcher.fd, dir, IN_MODIFY);
    if(server.config_watcher.wd < 0) {
        wlr_log(WLR_ERROR, "config watcher: failed to add directory `%s`", dir);
        close(server.config_watcher.fd);
        return;
    }

    server.config_watcher.source = wl_event_loop_add_fd(server.event_loop, server.config_watcher.fd,
            WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR, watch_callback, NULL);
}

void
config_watcher_deinit(void) {
    wl_event_source_remove(server.config_watcher.source);
    server.config_watcher.source = NULL;

    inotify_rm_watch(server.config_watcher.fd, server.config_watcher.wd);
    close(server.config_watcher.fd);
}

bool
config_watcher_running(void) {
    return server.config_watcher.source != NULL;
}
