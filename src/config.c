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
#define STRING_IMPLEMENTATION
#include "dyn_string.h"
#include "helpers.h"
#include "keybinds.h"
#include "keyboard.h"
#include "layer_surface.h"
#include "layout.h"
#include "mwc.h"
#include "output.h"
#include "pointer.h"
#include "rules.h"
#include "toplevel.h"
#include "workspace.h"

// this is a helper for logging the config errors
uint32_t line_number;
#define ERROR(msg, ...) wlr_log(WLR_ERROR, "config: line %u: " msg, line_number, ##__VA_ARGS__)

// assumes valid hex
static uint32_t
hex_to_unsigned_decimal(char *hex, size_t len) {
    uint32_t result = 0;
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

static bool
try_parse_color(char *s, struct color *dest) {
    size_t len = strlen(s);
    if(len != 6 && len != 8)
        return false;

    if(len == 6) {
        dest->r = clamp(hex_to_unsigned_decimal(s + 0, 2), 0, 255);
        dest->g = clamp(hex_to_unsigned_decimal(s + 2, 2), 0, 255);
        dest->b = clamp(hex_to_unsigned_decimal(s + 4, 2), 0, 255);
        dest->a = 255;
    } else if(len == 8) {
        dest->r = clamp(hex_to_unsigned_decimal(s + 0, 2), 0, 255);
        dest->g = clamp(hex_to_unsigned_decimal(s + 2, 2), 0, 255);
        dest->b = clamp(hex_to_unsigned_decimal(s + 4, 2), 0, 255);
        dest->a = clamp(hex_to_unsigned_decimal(s + 6, 2), 0, 255);
    }

    return true;
}

static bool
config_add_layer_rule(struct config *c, char *regex, char *predicate, char **args, size_t arg_count) {
    struct layer_rule_regex condition;
    if(strcmp(regex, "_") == 0) {
        condition.has = false;
    } else {
        regex_t compiled;
        if(regcomp(&compiled, regex, REG_EXTENDED) != 0) {
            ERROR("%s is not a valid regex", regex);
            return false;
        }
        condition.regex = compiled;
        condition.has = true;
    }

    if(strcmp(predicate, "blur") == 0) {
        array_push(&c->layer_rules.blur,
                ((struct layer_rule_blur){
                        .condition = condition,
                        .optimized = arg_count > 0 && atoi(args[0]),
                        .ignore_transparent = arg_count > 1 && atoi(args[1]),
                }));
    } else {
        ERROR("invalid layer rule %s", predicate);
        if(condition.has) {
            regfree(&condition.regex);
        }
        return false;
    }

    return true;
}

static bool
config_add_window_rule(struct config *c, char *app_id_regex, char *title_regex, char *predicate, char **args,
        size_t arg_count) {
    struct window_rule_regex condition;
    if(strcmp(app_id_regex, "_") == 0) {
        condition.has_app_id_regex = false;
    } else {
        regex_t compiled;
        if(regcomp(&compiled, app_id_regex, REG_EXTENDED) != 0) {
            ERROR("`%s` is not a valid regex", app_id_regex);
            return false;
        }
        condition.app_id_regex = compiled;
        condition.has_app_id_regex = true;
    }

    if(strcmp(title_regex, "_") == 0) {
        condition.has_title_regex = false;
    } else {
        regex_t compiled;
        if(regcomp(&compiled, title_regex, REG_EXTENDED) != 0) {
            ERROR("`%s` is not a valid regex", title_regex);
            return false;
        }
        condition.title_regex = compiled;
        condition.has_title_regex = true;
    }

    if(strcmp(predicate, "float") == 0) {
        array_push(&c->window_rules.floating,
                ((struct window_rule){
                        .condition = condition,
                }));
    } else if(strcmp(predicate, "size") == 0) {
        if(arg_count < 2)
            goto invalid;

        struct window_rule_size window_rule;
        window_rule.condition = condition;

        // if it ends with '%' we treat it as a relative unit
        if(args[0][strlen(args[0]) - 1] == '%') {
            args[0][strlen(args[0]) - 1] = 0;
            window_rule.relative_width = true;
        }
        if(args[1][strlen(args[1]) - 1] == '%') {
            args[1][strlen(args[1]) - 1] = 0;
            window_rule.relative_height = true;
        }

        window_rule.width = max(atoi(args[0]), 0);
        window_rule.height = max(atoi(args[1]), 0);

        array_push(&c->window_rules.size, window_rule);
    } else if(strcmp(predicate, "opacity") == 0) {
        if(arg_count < 1)
            goto invalid;

        double active = clamp(atof(args[0]), 0.0, 1.0);
        double inactive = arg_count > 1 ? clamp(atof(args[1]), 0.0, 1.0) : active;

        array_push(&c->window_rules.opacity,
                ((struct window_rule_opacity){
                        .condition = condition,
                        .active_value = active,
                        .inactive_value = inactive,
                }));
    } else if(strcmp(predicate, "no_titlebar") == 0) {
        array_push(&c->window_rules.no_titlebar,
                ((struct window_rule){
                        .condition = condition,
                }));
    } else if(strcmp(predicate, "no_border") == 0) {
        array_push(&c->window_rules.no_border,
                ((struct window_rule){
                        .condition = condition,
                }));
    } else if(strcmp(predicate, "no_shadow") == 0) {
        array_push(&c->window_rules.no_shadow,
                ((struct window_rule){
                        .condition = condition,
                }));
    } else if(strcmp(predicate, "no_blur") == 0) {
        array_push(&c->window_rules.no_blur,
                ((struct window_rule){
                        .condition = condition,
                }));
    } else {
        ERROR("invalid window_rule `%s`", predicate);
        goto cleanup;
    }

    return true;

invalid:
    ERROR("invalid args to window_rule `%s`", predicate);
cleanup:
    if(condition.has_app_id_regex) {
        regfree(&condition.app_id_regex);
    }
    if(condition.has_title_regex) {
        regfree(&condition.title_regex);
    }
    return false;
}

static char *
string_append_with_comma(char *a, char *b, size_t *cap, bool comma) {
    // append this while making sure there is enough space
    size_t a_len = strlen(a);
    size_t b_len = strlen(b);
    while(*cap < a_len + b_len + 2) {
        *cap *= 2;
        a = realloc(a, *cap);
    }

    // now there is enough space to fit the new one; we add , if its not the first one
    if(comma) {
        a[a_len] = ',';
        a_len++;
    }
    // and then copy the thing over
    char *p = b;
    char *q = &a[a_len];
    while(*p != 0) {
        *q = *p;
        p++;
        q++;
    }
    *q = 0;

    return a;
}

// handle appending to the config string
static void
config_add_keymap(struct config *c, char *layout, char *variant) {
    if(c->keymap_layouts == NULL) {
        // it has not been allocated yet
        c->keymap_layouts = string_new(NULL);
        c->keymap_variants = string_new(NULL);
        string_append_string(&c->keymap_layouts, layout);
        string_append_string(&c->keymap_variants, variant);
    }

    string_append(&c->keymap_layouts, ',');
    string_append(&c->keymap_variants, ',');
    string_append_string(&c->keymap_layouts, layout);
    string_append_string(&c->keymap_variants, variant);
}

static bool
config_add_keybind(struct config *c, char *modifiers, char *key, char *action, char **args, size_t arg_count) {
    char *p = modifiers;
    uint32_t modifiers_flag = 0;

    while(*p != '\0') {
        char *mod = string_new(NULL);
        while(*p != '+' && *p != '\0') {
            string_append(&mod, *p);
            p++;
        }

        if(strcmp(mod, "alt") == 0) {
            modifiers_flag |= WLR_MODIFIER_ALT;
        } else if(strcmp(mod, "super") == 0) {
            modifiers_flag |= WLR_MODIFIER_LOGO;
        } else if(strcmp(mod, "ctrl") == 0) {
            modifiers_flag |= WLR_MODIFIER_CTRL;
        } else if(strcmp(mod, "shift") == 0) {
            modifiers_flag |= WLR_MODIFIER_SHIFT;
        }

        string_destroy(mod);

        if(*p == '+')
            p++;
    }

    uint32_t key_sym = 0;
    bool pointer = false;
    if(strncmp(key, "mouse_", 6) == 0) {
        pointer = true;
        key = key + 6;
        if(strcmp(key, "left_click") == 0) {
            key_sym = 272;
        } else if(strcmp(key, "right_click") == 0) {
            key_sym = 273;
        } else if(strcmp(key, "middle_click") == 0) {
            key_sym = 274;
        } else {
            key_sym = atoi(key);
        }
    } else if(strncmp(key, "pointer_", 8) == 0) {
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
    } else {
        if(strcmp(key, "return") == 0 || strcmp(key, "enter") == 0) {
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
            if(key_sym == 0) {
                ERROR("key `%s` doesn't seem right", key);
                return false;
            }
        }
    }

    struct keybind keybind = (struct keybind){
            .modifiers = modifiers_flag,
            .key = key_sym,
    };

    // this is immediately true for most, needs to be set to false if otherwise
    keybind.initialized = true;

    if(strcmp(action, "exit") == 0) {
        keybind.action = keybind_stop_server;
    } else if(strcmp(action, "run") == 0) {
        if(arg_count < 1)
            goto invalid;

        keybind.action = keybind_run;
        keybind.args = strdup(args[0]);
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
        if(arg_count < 1)
            goto invalid;

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
            goto invalid;
        }

        keybind.action = keybind_move_focus;
        keybind.args = (void *)direction;
    } else if(strcmp(action, "move") == 0) {
        if(arg_count < 1)
            goto invalid;

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
            goto invalid;
        }

        keybind.action = keybind_move;
        keybind.args = (void *)direction;
    } else if(strcmp(action, "workspace") == 0) {
        if(arg_count < 1)
            goto invalid;

        keybind.action = keybind_change_workspace;
        // this is going to be overriden by the actual workspace that is needed for change_workspace()
        keybind.args = (void *)(uintptr_t)atoi(args[0]);
        keybind.initialized = false;
    } else if(strcmp(action, "move_to_workspace") == 0) {
        if(arg_count < 1)
            goto invalid;

        keybind.action = keybind_move_to_workspace;
        // this is going to be overriden by the actual workspace that is needed for change_workspace()
        keybind.args = (void *)(uintptr_t)atoi(args[0]);
        keybind.initialized = false;
    } else if(strcmp(action, "next_workspace") == 0) {
        keybind.action = keybind_next_workspace;
    } else if(strcmp(action, "prev_workspace") == 0) {
        keybind.action = keybind_prev_workspace;
    } else if(strcmp(action, "toggle_fullscreen") == 0) {
        keybind.action = keybind_toggle_fullscreen;
    } else if(strcmp(action, "increase_master_ratio") == 0) {
        if(arg_count < 1)
            goto invalid;

        keybind.action = keybind_increase_master_ratio;
        keybind.args = (void *)(uintptr_t)(atof(args[0]) * 100);
    } else if(strcmp(action, "decrease_master_ratio") == 0) {
        if(arg_count < 1)
            goto invalid;

        keybind.action = keybind_decrease_master_ratio;
        keybind.args = (void *)(uintptr_t)(atof(args[0]) * 100);
    } else {
        ERROR("invalid keybind action `%s`", action);
        return false;
    }

    if(pointer) {
        array_push(&c->pointer_keybinds, keybind);
    } else {
        array_push(&c->keybinds, keybind);
    }

    return true;

invalid:
    ERROR("invalid args to keybind `%s`", action);
    return false;
}

static void
destroy_args(char **args) {
    for(size_t i = 0; i < array_len(args); i++) {
        string_destroy(args[i]);
    }

    array_destroy(args);
}

static bool
handle_value(struct config *c, char *keyword, char **args) {
    size_t arg_count = array_len(args);

    if(strcmp(keyword, "keyboard_rate") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->keyboard_rate = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "keyboard_delay") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->keyboard_delay = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "pointer_sensitivity") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->pointer_sensitivity = clamp(atof(args[0]), -1.0, 1.0);
    } else if(strcmp(keyword, "pointer_acceleration") == 0) {
        if(arg_count < 1)
            goto invalid;
        c->pointer_acceleration =
                atoi(args[0]) ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
    } else if(strcmp(keyword, "pointer") == 0) {
        if(arg_count < 3)
            goto invalid;

        array_push(&c->pointers,
                ((struct pointer_config){
                        .name = strdup(args[0]),
                        .acceleration = atoi(args[1]) ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
                                                      : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT,
                        .sensitivity = clamp(atof(args[2]), -1.0, 1.0),
                }));
    } else if(strcmp(keyword, "pointer_left_handed") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->pointer_left_handed = atoi(args[0]);
    } else if(strcmp(keyword, "trackpad_disable_while_typing") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->trackpad_disable_while_typing = atoi(args[0]);
    } else if(strcmp(keyword, "trackpad_natural_scroll") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->trackpad_natural_scroll = atoi(args[0]);
    } else if(strcmp(keyword, "trackpad_tap_to_click") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->trackpad_tap_to_click = atoi(args[0]);
    } else if(strcmp(keyword, "trackpad_scroll_method") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(strcmp(args[0], "no_scroll") == 0) {
            c->trackpad_scroll_method = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
        } else if(strcmp(args[0], "two_fingers") == 0) {
            c->trackpad_scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
        } else if(strcmp(args[0], "edge") == 0) {
            c->trackpad_scroll_method = LIBINPUT_CONFIG_SCROLL_EDGE;
        } else if(strcmp(args[0], "on_button_down") == 0) {
            c->trackpad_scroll_method = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
        } else {
            goto invalid;
        }
    } else if(strcmp(keyword, "borders") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->borders = atoi(args[0]);
    } else if(strcmp(keyword, "border_width") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->border_width = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "outer_gaps") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->outer_gaps = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "inner_gaps") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->inner_gaps = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "master_ratio") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->master_ratio = clamp(atof(args[0]), 0, 1);
    } else if(strcmp(keyword, "master_count") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->master_count = max(atoi(args[0]), 1);
    } else if(strcmp(keyword, "cursor_theme") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->cursor_theme = strdup(args[0]);
    } else if(strcmp(keyword, "cursor_size") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->cursor_size = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "border_color") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(!try_parse_color(args[0], &c->border_color.active)) {
            goto invalid;
        }

        if(arg_count == 1 || !try_parse_color(args[1], &c->border_color.inactive)) {
            c->border_color.inactive = c->border_color.active;
        }
    } else if(strcmp(keyword, "output_mode") == 0) {
        if(arg_count < 4)
            goto invalid;

        array_push(&c->output_modes,
                ((struct output_mode_config){
                        .name = strdup(args[0]),
                        .width = atoi(args[1]),
                        .height = atoi(args[2]),
                        .refresh_rate = atoi(args[3]) * 1000,
                        // scale is optional, defaults to 1
                        .scale = arg_count > 4 ? atof(args[4]) : 1.0,
                }));
    } else if(strcmp(keyword, "output_position") == 0) {
        if(arg_count < 2)
            goto invalid;

        array_push(&c->output_positions,
                ((struct output_position_config){
                        .name = strdup(args[0]),
                        .x = atoi(args[1]),
                        .y = atoi(args[2]),
                }));
    } else if(strcmp(keyword, "workspace") == 0) {
        if(arg_count < 2)
            goto invalid;

        array_push(&c->workspaces,
                ((struct workspace_config){
                        .index = atoi(args[0]),
                        .output = strdup(args[1]),
                }));
    } else if(strcmp(keyword, "run") == 0) {
        if(arg_count < 1)
            goto invalid;

        array_push(&c->run, strdup(args[0]));
    } else if(strcmp(keyword, "keybind") == 0) {
        if(arg_count < 3)
            goto invalid;

        config_add_keybind(c, args[0], args[1], args[2], &args[3], arg_count - 3);
    } else if(strcmp(keyword, "env") == 0) {
        if(arg_count < 2)
            goto invalid;

        setenv(args[0], args[1], true);
    } else if(strcmp(keyword, "window_rule") == 0) {
        if(arg_count < 3)
            goto invalid;

        config_add_window_rule(c, args[0], args[1], args[2], &args[3], arg_count - 3);
    } else if(strcmp(keyword, "animations") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->animations = atoi(args[0]);
    } else if(strcmp(keyword, "animation_duration") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->animation_duration = clamp(atoi(args[0]), 0, INT_MAX);
    } else if(strcmp(keyword, "animation_curve") == 0) {
        if(arg_count < 4)
            goto invalid;

        c->animation_curve =
                fx_animation_curve_create((double[4]){atof(args[0]), atof(args[1]), atof(args[2]), atof(args[3])});
    } else if(strcmp(keyword, "client_side_decorations") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->client_side_decorations = atoi(args[0]);
    } else if(strcmp(keyword, "opacity") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->opacity.active = clamp(atof(args[0]), 0.0, 1.0);
        c->opacity.inactive = arg_count > 1 ? clamp(atof(args[1]), 0.0, 1.0) : c->opacity.active;
    } else if(strcmp(keyword, "opacity_apply_when_fullscreen") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->opacity_apply_when_fullscreen = atoi(args[0]);
    } else if(strcmp(keyword, "keymap") == 0) {
        if(arg_count < 2)
            goto invalid;

        config_add_keymap(c, args[0], args[1]);
    } else if(strcmp(keyword, "keymap_options") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->keymap_options = strdup(args[0]);
    } else if(strcmp(keyword, "border_radius") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->border_radius = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "border_radius_location") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(strcmp(args[0], "all") == 0) {
            c->border_radius_location = CORNER_LOCATION_ALL;
        } else {
            for(size_t i = 0; i < arg_count; i++) {
                if(strcmp(args[i], "top") == 0) {
                    c->border_radius_location |= CORNER_LOCATION_TOP;
                } else if(strcmp(args[i], "bottom") == 0) {
                    c->border_radius_location |= CORNER_LOCATION_BOTTOM;
                } else if(strcmp(args[i], "right") == 0) {
                    c->border_radius_location |= CORNER_LOCATION_RIGHT;
                } else if(strcmp(args[i], "left") == 0) {
                    c->border_radius_location |= CORNER_LOCATION_LEFT;
                } else if(strcmp(args[i], "top_right") == 0) {
                    c->border_radius_location |= CORNER_LOCATION_TOP_RIGHT;
                } else if(strcmp(args[i], "bottom_right") == 0) {
                    c->border_radius_location |= CORNER_LOCATION_BOTTOM_RIGHT;
                } else if(strcmp(args[i], "bottom_left") == 0) {
                    c->border_radius_location |= CORNER_LOCATION_BOTTOM_LEFT;
                } else if(strcmp(args[i], "top_left") == 0) {
                    c->border_radius_location |= CORNER_LOCATION_TOP_LEFT;
                }
            }
        }
    } else if(strcmp(keyword, "blur") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->blur = atoi(args[0]);
    } else if(strcmp(keyword, "blur_optimized") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(strcmp(args[0], "always") == 0) {
            c->blur_optimized = BLUR_OPTIMIZED_ALWAYS;
        } else if(strcmp(args[0], "tiled_only") == 0) {
            c->blur_optimized = BLUR_OPTIMIZED_TILED_ONLY;
        } else if(strcmp(args[0], "never") == 0) {
            c->blur_optimized = BLUR_OPTIMIZED_NEVER;
        } else {
            goto invalid;
        }
    } else if(strcmp(keyword, "blur_passes") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->blur_params.num_passes = max(atoi(args[0]), 1);
    } else if(strcmp(keyword, "blur_radius") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->blur_params.radius = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "blur_noise") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->blur_params.noise = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "blur_brightness") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->blur_params.brightness = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "blur_contrast") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->blur_params.contrast = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "blur_saturation") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->blur_params.saturation = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "shadows") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->shadows = atoi(args[0]);
    } else if(strcmp(keyword, "shadow_size") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->shadow_size = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "shadow_blur") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->shadow_blur = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "shadow_position") == 0) {
        if(arg_count < 2)
            goto invalid;

        c->shadow_position.x = atoi(args[0]);
        c->shadow_position.y = atoi(args[1]);
    } else if(strcmp(keyword, "shadow_color") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(!try_parse_color(args[0], &c->shadow_color))
            goto invalid;
    } else if(strcmp(keyword, "layer_rule") == 0) {
        if(arg_count < 2)
            goto invalid;

        config_add_layer_rule(c, args[0], args[1], &args[2], arg_count - 2);
    } else if(strcmp(keyword, "titlebars") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->titlebars = atoi(args[0]);
    } else if(strcmp(keyword, "titlebar_height") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->titlebar_height = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "titlebar_color") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(!try_parse_color(args[0], &c->titlebar_color.active))
            goto invalid;

        if(arg_count == 1 || !try_parse_color(args[1], &c->titlebar_color.inactive)) {
            c->titlebar_color.inactive = c->titlebar_color.active;
        }
    } else if(strcmp(keyword, "titlebar_include_close_button") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->titlebar_include_close_button = atoi(args[0]);
    } else if(strcmp(keyword, "titlebar_close_button_size") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->titlebar_close_button_size = atoi(args[0]);
    } else if(strcmp(keyword, "titlebar_close_button_position") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(strcmp(args[0], "left") == 0) {
            c->titlebar_close_button_position = TITLEBAR_CLOSE_BUTTON_POSITION_LEFT;
        } else if(strcmp(args[0], "right") == 0) {
            c->titlebar_close_button_position = TITLEBAR_CLOSE_BUTTON_POSITION_RIGHT;
        } else {
            goto invalid;
        }
    } else if(strcmp(keyword, "titlebar_close_button_padding") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->titlebar_close_button_padding.left = atoi(args[0]);
        c->titlebar_close_button_padding.right = arg_count > 1 ? atoi(args[1]) : c->titlebar_close_button_padding.left;
    } else if(strcmp(keyword, "titlebar_close_button_shape") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(strcmp(args[0], "square") == 0) {
            c->titlebar_close_button_shape = TITLEBAR_CLOSE_BUTTON_SHAPE_SQUARE;
        } else if(strcmp(args[0], "circle") == 0) {
            c->titlebar_close_button_shape = TITLEBAR_CLOSE_BUTTON_SHAPE_CIRCLE;
        } else {
            goto invalid;
        }
    } else if(strcmp(keyword, "titlebar_close_button_color") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(!try_parse_color(args[0], &c->titlebar_close_button_color.active))
            goto invalid;

        if(arg_count == 1 || !try_parse_color(args[1], &c->titlebar_close_button_color.inactive)) {
            c->titlebar_close_button_color.inactive = c->titlebar_close_button_color.active;
        }
    } else if(strcmp(keyword, "titlebar_include_title") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->titlebar_include_title = atoi(args[0]);
    } else if(strcmp(keyword, "titlebar_center_title") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->titlebar_center_title = atoi(args[0]);
    } else if(strcmp(keyword, "titlebar_title_padding") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->titlebar_title_padding.left = atoi(args[0]);
        c->titlebar_title_padding.right = arg_count > 1 ? atoi(args[1]) : c->titlebar_title_padding.left;
    } else if(strcmp(keyword, "titlebar_title_color") == 0) {
        if(arg_count < 1)
            goto invalid;

        if(!try_parse_color(args[0], &c->titlebar_title_color)) {
            goto invalid;
        }
    } else if(strcmp(keyword, "titlebar_title_font") == 0) {
        if(arg_count < 1)
            goto invalid;

        c->font = fcft_from_name(1, (const char **)&args[0], NULL);
        if(c->font == NULL) {
            ERROR("error while loading the font `%s`, titles wont be drawn", args[0]);
        }
    } else {
        ERROR("invalid keyword `%s`", keyword);
        string_destroy(keyword);
        destroy_args(args);
        return false;
    }

    string_destroy(keyword);
    destroy_args(args);
    return true;

invalid:
    ERROR("invalid args to `%s`", keyword);
    string_destroy(keyword);
    destroy_args(args);
    return false;
}

static void
get_default_config_path(char *dest, size_t size) {
    char *default_config_path = getenv("DEFAULT_CONFIG_PATH");

    if(default_config_path == NULL) {
        default_config_path = "/usr/share/mwc/default.conf";
        wlr_log(WLR_INFO, "no env DEFAULT_CONFIG_PATH set, using the default `%s`", default_config_path);
    } else {
        wlr_log(WLR_INFO, "env DEFAULT_CONFIG_PATH set to `%s`, using it", default_config_path);
    }

    strncpy(dest, default_config_path, size);
    dest[size - 1] = 0;
}

static bool
get_config_path(char *dest, size_t size) {
    char *env_conf = getenv("CONFIG_PATH");
    if(env_conf != NULL) {
        strncpy(dest, env_conf, size);
        dest[size - 1] = 0;
        return true;
    }

    char *config_home = getenv("XDG_CONFIG_HOME");
    if(config_home != NULL) {
        snprintf(dest, size, "%s/mwc/mwc.conf", config_home);
        return true;
    }

    char *home = getenv("HOME");
    if(home != NULL) {
        snprintf(dest, size, "%s/.config/mwc/mwc.conf", home);
        return true;
    }

    return false;
}

// returns `false` if the line is empty, else returns `true` with the parametars extracted into the passed pointers.
// `*keyword` is a string, and `*arguments` is an array of strings. assumes the line is newline terminated, as it should
// be with `fgets()`
static bool
parse_line(char *line, char **keyword, char ***arguments) {
    char *p = line;

    // skip whitespace
    while(*p == ' ' || *p == '\t')
        p++;

    // if its an empty line or it starts with '#' (comment) skip
    if(*p == '\n' || *p == '#')
        return false;

    // we create these more ergonomic variables to use; will 'return' them at the end
    char *kw = string_new(NULL);
    char **args;
    array_init(&args);

    while(*p != ' ' && *p != '\t' && *p != '\n') {
        string_append(&kw, *p);
        p++;
    }

    // skip whitespace
    while(*p == ' ' || *p == '\t')
        p++;

    while(*p != '\n') {
        char *arg = string_new(NULL);

        bool word = false;
        if(*p == '\"') {
            word = true;
            p++;
        };

        while((word && *p != '\"' && *p != '\n') || (!word && *p != ' ' && *p != '\t' && *p != '\n')) {
            if(word && *p == '\\' && *(p + 1) == '\"') {
                // escape quotes
                string_append(&arg, '\"');
                p += 2;
            } else if(word && *p == '\\' && *(p + 1) == '\\') {
                // escape double backslash
                string_append(&arg, '\\');
                p += 2;
            } else {
                string_append(&arg, *p);
                p++;
            }
        }

        array_push(&args, arg);

        if(word)
            p++;

        // skip whitespace
        while(*p == ' ' || *p == '\t')
            p++;
    }

    *keyword = kw;
    *arguments = args;

    return true;
}

static void
set_default_needed_params(struct config *c) {
    // as we are initializing config with calloc, some fields that are necessary in order for mwc to not crash may be
    // not specified in the config. we set their values to some default value
    if(c->keyboard_rate == 0) {
        c->keyboard_rate = 150;
        wlr_log(WLR_INFO, "keyboard_rate not specified. using default %u", c->keyboard_rate);
    }
    if(c->keyboard_delay == 0) {
        c->keyboard_delay = 50;
        wlr_log(WLR_INFO, "keyboard_delay not specified. using default %u", c->keyboard_delay);
    }
    if(c->cursor_size == 0) {
        c->cursor_size = 24;
        wlr_log(WLR_INFO, "cursor_size not specified. using default %u", c->cursor_size);
    }
    if(c->master_count == 0) {
        c->master_count = 1;
        wlr_log(WLR_INFO, "master_count not specified. using default %u", c->master_count);
    }
    if(c->master_ratio == 0) {
        // here we evenly space toplevels if there is no master_ratio specified
        c->master_ratio = c->master_count / (double)(c->master_count + 1);
        wlr_log(WLR_INFO, "master_ratio not specified. using default %lf", c->master_ratio);
    }
    if(c->animations && c->animation_duration == 0) {
        c->animation_duration = 500;
        wlr_log(WLR_INFO, "animation_duration not specified. using default %u", c->animation_duration);
    }
    if(c->animations && c->animation_curve == NULL) {
        c->animation_curve = fx_animation_curve_create((double[4]){0});
        wlr_log(WLR_INFO, "animation_curve not specified. using linear");
    }
    if(c->opacity.active == 0) {
        c->opacity.active = 1.0;
        c->opacity.inactive = 1.0;
        wlr_log(WLR_INFO, "opacity not specified. using default %lf %lf", c->opacity.active, c->opacity.inactive);
    }
    if(c->border_radius_location == 0) {
        c->border_radius_location = CORNER_LOCATION_ALL;
        wlr_log(WLR_INFO, "border_radius_location not specified. using all");
    }
    if(c->titlebar_close_button_size > c->titlebar_height) {
        wlr_log(WLR_INFO, "titlebar_close_button_size (%u) larger than titlebar_height (%u). setting it to %u",
                c->titlebar_close_button_size, c->titlebar_height, c->titlebar_height);
        c->titlebar_close_button_size = c->titlebar_height;
    }
}

struct config *
config_load() {
    struct config *c = calloc(1, sizeof(*c));

    FILE *config_file;
    char path[1024];
    if(get_config_path(path, sizeof(path))) {
        config_file = fopen(path, "r");
        if(config_file != NULL) {
            char *current = path;
            char *last_slash = NULL;
            while(*current != 0) {
                if(*current == '/') {
                    last_slash = current;
                }
                current++;
            }

            assert(last_slash != NULL);
            *last_slash = 0;
            c->dir = strdup(path);
        } else {
            wlr_log(WLR_INFO, "couldn't open the config file");
            get_default_config_path(path, sizeof(path));
            config_file = fopen(path, "r");
        }
    } else {
        wlr_log(WLR_INFO, "couldn't get config file path, backing to default config");
        get_default_config_path(path, sizeof(path));
        config_file = fopen(path, "r");
    }

    if(config_file == NULL) {
        wlr_log(WLR_ERROR, "couldn't open the default config file");
        free(c);
        return NULL;
    }

    // initialize all of the arrays
    array_init(&c->keybinds);
    array_init(&c->pointer_keybinds);
    array_init(&c->output_modes);
    array_init(&c->output_positions);
    array_init(&c->workspaces);
    array_init(&c->pointers);
    array_init(&c->window_rules.floating);
    array_init(&c->window_rules.size);
    array_init(&c->window_rules.opacity);
    array_init(&c->window_rules.no_titlebar);
    array_init(&c->window_rules.no_border);
    array_init(&c->window_rules.no_shadow);
    array_init(&c->window_rules.no_blur);

    array_init(&c->layer_rules.blur);

    array_init(&c->run);

    char *keyword, **args;
    line_number = 1;

    // you aint gonna have lines longer than this
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), config_file) != NULL) {
        if(parse_line(buffer, &keyword, &args)) {
            handle_value(c, keyword, args);
        }

        line_number++;
    }

    fclose(config_file);
    set_default_needed_params(c);

    return c;
}

void
config_destroy(struct config *c) {
    if(c->dir != NULL) {
        free(c->dir);
    }

    for(struct output_mode_config *iter = c->output_modes; iter <= array_last(c->output_modes); iter++) {
        free(iter->name);
    }
    array_destroy(c->output_modes);

    for(struct output_position_config *iter = c->output_positions; iter <= array_last(c->output_positions); iter++) {
        free(iter->name);
    }
    array_destroy(c->output_positions);

    for(struct workspace_config *iter = c->workspaces; iter <= array_last(c->workspaces); iter++) {
        free(iter->output);
    }
    array_destroy(c->workspaces);

    for(struct keybind *iter = c->keybinds; iter <= array_last(c->keybinds); iter++) {
        if(iter->action == keybind_run) {
            free(iter->args);
        }
    }
    array_destroy(c->keybinds);

    // here we dont allocate anything more than just a struct
    array_destroy(c->pointer_keybinds);

    // destroy window rules
    for(struct window_rule *iter = c->window_rules.floating; iter <= array_last(c->window_rules.floating); iter++) {
        if(iter->condition.has_app_id_regex) {
            regfree(&iter->condition.app_id_regex);
        }
        if(iter->condition.has_title_regex) {
            regfree(&iter->condition.title_regex);
        }
    }
    array_destroy(c->window_rules.floating);

    for(struct window_rule_size *iter = c->window_rules.size; iter <= array_last(c->window_rules.size); iter++) {
        if(iter->condition.has_app_id_regex) {
            regfree(&iter->condition.app_id_regex);
        }
        if(iter->condition.has_title_regex) {
            regfree(&iter->condition.title_regex);
        }
    }
    array_destroy(c->window_rules.size);

    for(struct window_rule_opacity *iter = c->window_rules.opacity; iter <= array_last(c->window_rules.opacity);
            iter++) {
        if(iter->condition.has_app_id_regex) {
            regfree(&iter->condition.app_id_regex);
        }
        if(iter->condition.has_title_regex) {
            regfree(&iter->condition.title_regex);
        }
    }
    array_destroy(c->window_rules.opacity);

    for(struct window_rule *iter = c->window_rules.no_titlebar; iter <= array_last(c->window_rules.no_titlebar);
            iter++) {
        if(iter->condition.has_app_id_regex) {
            regfree(&iter->condition.app_id_regex);
        }
        if(iter->condition.has_title_regex) {
            regfree(&iter->condition.title_regex);
        }
    }
    array_destroy(c->window_rules.no_titlebar);

    for(struct window_rule *iter = c->window_rules.no_border; iter <= array_last(c->window_rules.no_border); iter++) {
        if(iter->condition.has_app_id_regex) {
            regfree(&iter->condition.app_id_regex);
        }
        if(iter->condition.has_title_regex) {
            regfree(&iter->condition.title_regex);
        }
    }
    array_destroy(c->window_rules.no_border);

    for(struct window_rule *iter = c->window_rules.no_shadow; iter <= array_last(c->window_rules.no_shadow); iter++) {
        if(iter->condition.has_app_id_regex) {
            regfree(&iter->condition.app_id_regex);
        }
        if(iter->condition.has_title_regex) {
            regfree(&iter->condition.title_regex);
        }
    }
    array_destroy(c->window_rules.no_shadow);

    for(struct window_rule *iter = c->window_rules.no_blur; iter <= array_last(c->window_rules.no_blur); iter++) {
        if(iter->condition.has_app_id_regex) {
            regfree(&iter->condition.app_id_regex);
        }
        if(iter->condition.has_title_regex) {
            regfree(&iter->condition.title_regex);
        }
    }
    array_destroy(c->window_rules.no_blur);

    // destroy layer rules
    for(struct layer_rule_blur *iter = c->layer_rules.blur; iter <= array_last(c->layer_rules.blur); iter++) {
        if(iter->condition.has) {
            regfree(&iter->condition.regex);
        }
    }
    array_destroy(c->layer_rules.blur);

    string_destroy(c->keymap_layouts);
    string_destroy(c->keymap_variants);
    free(c->keymap_options);

    for(struct pointer_config *iter = c->pointers; iter <= array_last(c->pointers); iter++) {
        free(iter->name);
    }
    array_destroy(c->pointers);

    if(c->font != NULL) {
        fcft_destroy(c->font);
    }

    free(c->cursor_theme);

    fx_animation_curve_destroy(c->animation_curve);

    for(char **iter = c->run; iter <= array_last(c->run); iter++) {
        free(*iter);
    }
    array_destroy(c->run);

    free(c);
}

extern struct server server;

static void
layout_reorganize(struct workspace *workspace) {
    uint32_t master_count = wl_list_length(&workspace->masters);
    if(master_count > server.config->master_count) {
        while(master_count > server.config->master_count) {
            demote_last_master(workspace);
            master_count--;
        }
    } else {
        uint32_t slave_count = wl_list_length(&workspace->slaves);
        while(master_count < server.config->master_count && slave_count > 0) {
            promote_last_slave(workspace);
            master_count++;
            slave_count--;
        }
    }
}

void
config_reload() {
    struct config *c = config_load();
    if(c == NULL) {
        // if we couldnt load the config then skip the reload
        wlr_log(WLR_ERROR, "could not reload the config, keeping the old one");
        return;
    }

    // we destroy the old config and set the new one
    config_destroy(server.config);
    server.config = c;

    // send the decoration type to the clients
    // node: kde server decorations do not have this ability (at least in wlroots) so these toplevels will not reload
    wlr_server_decoration_manager_set_default_mode(server.kde_decoration_manager,
            c->client_side_decorations ? WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT
                                       : WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

    struct wlr_xdg_toplevel_decoration_v1 *iter_xdg_deco;
    wl_list_for_each(iter_xdg_deco, &server.xdg_decoration_manager->decorations, link) {
        wlr_xdg_toplevel_decoration_v1_set_mode(iter_xdg_deco,
                c->client_side_decorations ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                           : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    // set the new blur parametars
    wlr_scene_set_blur_data(server.scene, c->blur_params);

    // we reconfigure and reposition the outputs
    struct output *iter_output;
    wl_list_for_each(iter_output, &server.outputs, link) {
        output_modeset(iter_output->wlr_output);
        output_place_in_layout(iter_output);
        output_configure_blur(iter_output);

        // configure the layers; this needs to happen before configuring the toplevels, since it changes the usable area
        layer_surfaces_configure(iter_output);
        // recheck layer rules
        struct layer_surface *iter_layer_surface;
        for(size_t i = 0; i < 4; i++) {
            wl_list_for_each(iter_layer_surface, &(&iter_output->layers.background)[i], link) {
                layer_surface_check_rules(iter_layer_surface);
            }
        }

        struct workspace *iter_workspace;
        wl_list_for_each(iter_workspace, &iter_output->workspaces, link) {
            // we rewire the keybinds
            for(struct keybind *iter = c->keybinds; iter <= array_last(c->keybinds); iter++) {
                if(iter->action == keybind_change_workspace && (uintptr_t)iter->args == iter_workspace->index) {
                    iter->args = iter_workspace;
                    iter->initialized = true;
                } else if(iter->action == keybind_move_to_workspace && (uintptr_t)iter->args == iter_workspace->index) {
                    iter->args = iter_workspace;
                    iter->initialized = true;
                }
            }

            struct toplevel *iter_toplevel;
            wl_list_for_each(iter_toplevel, &iter_workspace->masters, link) {
                decoration_destroy_all(&iter_toplevel->decoration);
                toplevel_check_rules(iter_toplevel);
            }
            wl_list_for_each(iter_toplevel, &iter_workspace->slaves, link) {
                decoration_destroy_all(&iter_toplevel->decoration);
                toplevel_check_rules(iter_toplevel);
            }
            wl_list_for_each(iter_toplevel, &iter_workspace->floating, link) {
                decoration_destroy_all(&iter_toplevel->decoration);
                toplevel_check_rules(iter_toplevel);
            }

            if(iter_workspace->fullscreen != NULL) {
                decoration_destroy_all(&iter_workspace->fullscreen->decoration);
                toplevel_check_rules(iter_workspace->fullscreen);
            }

            // master_count might have changed in the new config, so we update the layout
            layout_reorganize(iter_workspace);
            // and than send the configures to all the tiled toplevels
            layout_configure(iter_workspace);
        }
    }

    if(server.grabbed_toplevel != NULL) {
        decoration_destroy_all(&server.grabbed_toplevel->decoration);
        toplevel_check_rules(server.grabbed_toplevel);
    }

    struct keyboard *keyboard;
    wl_list_for_each(keyboard, &server.keyboards, link) {
        keyboard_configure(keyboard);
    }

    struct pointer *pointer;
    wl_list_for_each(pointer, &server.pointers, link) {
        pointer_configure(pointer);
    }

    wlr_xcursor_manager_destroy(server.cursor_mgr);
    server.cursor_mgr = wlr_xcursor_manager_create(server.config->cursor_theme, server.config->cursor_size);

    if(server.config->cursor_theme != NULL) {
        setenv("XCURSOR_THEME", server.config->cursor_theme, true);
    } else {
        setenv("XCURSOR_THEME", "", true);
    }

    char cursor_size[8];
    snprintf(cursor_size, sizeof(cursor_size), "%u", server.config->cursor_size);
    setenv("XCURSOR_SIZE", cursor_size, true);
}

static void
idle_reload_config(void *data) {
    wlr_log(WLR_INFO, "reloading config");
    config_reload();
}

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (16 * (EVENT_SIZE + 16))

void *
config_watch(void *arg) {
    char *dir = arg;

    if(dir == NULL)
        return NULL;

    int inotify_fd = inotify_init();
    if(inotify_fd < 0) {
        wlr_log(WLR_ERROR, "inotify failed to start");
        return NULL;
    }

    int wd = inotify_add_watch(inotify_fd, dir, IN_MODIFY);
    if(wd < 0) {
        wlr_log(WLR_ERROR, "inotify failed to start");
        close(inotify_fd);
        return NULL;
    }

    char buffer[BUF_LEN];
    while(true) {
        ssize_t length = read(inotify_fd, buffer, BUF_LEN);
        if(length < 0) {
            wlr_log(WLR_ERROR, "inotify failed read");
            break;
        }

        for(char *ptr = buffer; ptr < buffer + length; ptr += EVENT_SIZE + ((struct inotify_event *)ptr)->len) {
            struct inotify_event *event = (struct inotify_event *)ptr;
            if(event->mask & IN_MODIFY) {
                wl_event_loop_add_idle(server.wl_event_loop, idle_reload_config, NULL);
            }
        }
    }

    inotify_rm_watch(inotify_fd, wd);
    close(inotify_fd);

    return NULL;
}
