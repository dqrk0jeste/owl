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

#include "keybinds.h"
#include "keyboard.h"
#include "layer_surface.h"
#include "layout.h"
#include "mwc.h"
#include "output.h"
#include "pointer.h"
#include "toplevel.h"
#include "workspace.h"

#define clamp(v, a, b) (max((a), min((v), (b))))

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
try_parse_color_hex(char *s, struct mwc_color *dest) {
    size_t len = strlen(s);
    if(len != 6 && len != 8) return false;

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
try_parse_color_rgba_or_hex(char **args, size_t arg_count, struct mwc_color *dest) {
    if(arg_count == 4) {
        dest->r = clamp(atoi(args[0]), 0, 255);
        dest->g = clamp(atoi(args[1]), 0, 255);
        dest->b = clamp(atoi(args[2]), 0, 255);
        dest->a = clamp(atoi(args[3]), 0, 255);
    } else if(strlen(args[0]) == 6) {
        dest->r = clamp(hex_to_unsigned_decimal(args[0] + 0, 2), 0, 255);
        dest->g = clamp(hex_to_unsigned_decimal(args[0] + 2, 2), 0, 255);
        dest->b = clamp(hex_to_unsigned_decimal(args[0] + 4, 2), 0, 255);
        dest->a = 255;
    } else if(strlen(args[0]) == 8) {
        dest->r = clamp(hex_to_unsigned_decimal(args[0] + 0, 2), 0, 255);
        dest->g = clamp(hex_to_unsigned_decimal(args[0] + 2, 2), 0, 255);
        dest->b = clamp(hex_to_unsigned_decimal(args[0] + 4, 2), 0, 255);
        dest->a = clamp(hex_to_unsigned_decimal(args[0] + 6, 2), 0, 255);
    } else {
        return false;
    }

    return true;
}

static bool
config_add_layer_rule(struct mwc_config *c, char *regex, char *predicate, char **args, size_t arg_count) {
    struct layer_rule_regex condition;
    if(strcmp(regex, "_") == 0) {
        condition.has = false;
    } else {
        regex_t compiled;
        if(regcomp(&compiled, regex, REG_EXTENDED) != 0) {
            wlr_log(WLR_ERROR, "%s is not a valid regex", regex);
            regfree(&compiled);
            return false;
        }
        condition.regex = compiled;
        condition.has = true;
    }

    if(strcmp(predicate, "blur") == 0) {
        struct layer_rule_blur *lr = calloc(1, sizeof(*lr));
        lr->condition = condition;
        wl_list_insert(&c->layer_rules.blur, &lr->link);
    } else {
        wlr_log(WLR_ERROR, "invalid layer rule %s", predicate);
        if(condition.has) {
            regfree(&condition.regex);
        }
        return false;
    }

    return true;
}

static bool
config_add_window_rule(struct mwc_config *c, char *app_id_regex, char *title_regex, char *predicate, char **args,
    size_t arg_count) {
    struct window_rule_regex condition;
    if(strcmp(app_id_regex, "_") == 0) {
        condition.has_app_id_regex = false;
    } else {
        regex_t compiled;
        if(regcomp(&compiled, app_id_regex, REG_EXTENDED) != 0) {
            wlr_log(WLR_ERROR, "%s is not a valid regex", app_id_regex);
            regfree(&compiled);
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
            wlr_log(WLR_ERROR, "%s is not a valid regex", title_regex);
            regfree(&compiled);
            return false;
        }
        condition.title_regex = compiled;
        condition.has_title_regex = true;
    }

    if(strcmp(predicate, "float") == 0) {
        struct window_rule_float *window_rule = calloc(1, sizeof(*window_rule));
        window_rule->condition = condition;
        wl_list_insert(&c->window_rules.floating, &window_rule->link);
    } else if(strcmp(predicate, "size") == 0) {
        if(arg_count < 2) {
            wlr_log(WLR_ERROR, "invalid args to window_rule %s", predicate);
            goto invalid;
            return false;
        }
        struct window_rule_size *window_rule = calloc(1, sizeof(*window_rule));
        window_rule->condition = condition;

        // if it ends with '%' we treat it as a relative unit
        if(args[0][strlen(args[0]) - 1] == '%') {
            args[0][strlen(args[0]) - 1] = 0;
            window_rule->relative_width = true;
        }
        if(args[1][strlen(args[1]) - 1] == '%') {
            args[1][strlen(args[1]) - 1] = 0;
            window_rule->relative_height = true;
        }

        window_rule->width = clamp(atoi(args[0]), 0, INT_MAX);
        window_rule->height = clamp(atoi(args[1]), 0, INT_MAX);

        wl_list_insert(&c->window_rules.size, &window_rule->link);
    } else if(strcmp(predicate, "opacity") == 0) {
        if(arg_count < 1) {
            wlr_log(WLR_ERROR, "invalid args to window_rule %s", predicate);
            goto invalid;
            return false;
        }
        struct window_rule_opacity *window_rule = calloc(1, sizeof(*window_rule));
        window_rule->condition = condition;

        window_rule->active_value = clamp(atof(args[0]), 0.0, 1.0);
        window_rule->inactive_value = arg_count > 1 ? clamp(atof(args[1]), 0.0, 1.0) : window_rule->active_value;

        wl_list_insert(&c->window_rules.opacity, &window_rule->link);
    } else if(strcmp(predicate, "no_titlebar") == 0) {
        struct window_rule_no_titlebar *window_rule = calloc(1, sizeof(*window_rule));
        window_rule->condition = condition;

        wl_list_insert(&c->window_rules.no_titlebar, &window_rule->link);
    } else {
        wlr_log(WLR_ERROR, "invalid window_rule %s", predicate);
        goto invalid;
    }

    return true;

invalid:
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
    /* append this while making sure there is enough space */
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

static void
config_add_keymap(struct mwc_config *c, char *layout, char *variant) {
    // everything here is ugly
    static size_t layout_cap, variant_cap;
    static size_t count;
    if(c->keymap_layouts == NULL) {
        // it has not been allocated yet
        layout_cap = STRING_INITIAL_LENGTH;
        c->keymap_layouts = calloc(layout_cap, sizeof(char));
        variant_cap = STRING_INITIAL_LENGTH;
        c->keymap_variants = calloc(variant_cap, sizeof(char));
        count = 0;
    }

    c->keymap_layouts = string_append_with_comma(c->keymap_layouts, layout, &layout_cap, count);
    c->keymap_variants = string_append_with_comma(c->keymap_variants, variant, &variant_cap, count);

    count++;
}

static bool
config_add_keybind(struct mwc_config *c, char *modifiers, char *key, char *action, char **args, size_t arg_count) {
    char *p = modifiers;
    uint32_t modifiers_flag = 0;

    while(*p != '\0') {
        char mod[64] = {0};
        char *q = mod;
        while(*p != '+' && *p != '\0') {
            *q = *p;
            p++;
            q++;
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

        if(*p == '+') {
            p++;
        }
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
                wlr_log(WLR_ERROR, "key %s doesn't seem right", key);
                return false;
            }
        }
    }

    struct keybind *k = calloc(1, sizeof(*k));
    *k = (struct keybind){
        .modifiers = modifiers_flag,
        .key = key_sym,
    };

    // this is true for most, needs to be set to false if otherwise
    k->initialized = true;

    if(strcmp(action, "exit") == 0) {
        k->action = keybind_stop_server;
    } else if(strcmp(action, "run") == 0) {
        if(arg_count < 1) {
            wlr_log(WLR_ERROR, "invalid args to %s", action);
            free(k);
            return false;
        }

        k->action = keybind_run;
        char *args_0_copy = strdup(args[0]);
        k->args = args_0_copy;
    } else if(strcmp(action, "kill_active") == 0) {
        k->action = keybind_close_keyboard_focused_toplevel;
    } else if(strcmp(action, "switch_floating_state") == 0 || strcmp(action, "toggle_floating") == 0) {
        k->action = keybind_focused_toplevel_toggle_floating;
    } else if(strcmp(action, "resize") == 0) {
        k->action = keybind_resize_focused_toplevel;
        k->stop = keybind_stop_resize_focused_toplevel;
    } else if(strcmp(action, "move") == 0) {
        k->action = keybind_move_focused_toplevel;
        k->stop = keybind_stop_move_focused_toplevel;
    } else if(strcmp(action, "move_focus") == 0) {
        if(arg_count < 1) {
            wlr_log(WLR_ERROR, "invalid args to %s", action);
            free(k);
            return false;
        }

        enum mwc_direction direction;
        if(strcmp(args[0], "up") == 0) {
            direction = MWC_UP;
        } else if(strcmp(args[0], "left") == 0) {
            direction = MWC_LEFT;
        } else if(strcmp(args[0], "down") == 0) {
            direction = MWC_DOWN;
        } else if(strcmp(args[0], "right") == 0) {
            direction = MWC_RIGHT;
        } else {
            wlr_log(WLR_ERROR, "invalid args to %s", action);
            free(k);
            return false;
        }

        k->action = keybind_move_focus;
        k->args = (void *)direction;
    } else if(strcmp(action, "swap") == 0) {
        if(arg_count < 1) {
            wlr_log(WLR_ERROR, "invalid args to %s", action);
            free(k);
            return false;
        }

        enum mwc_direction direction;
        if(strcmp(args[0], "up") == 0) {
            direction = MWC_UP;
        } else if(strcmp(args[0], "left") == 0) {
            direction = MWC_LEFT;
        } else if(strcmp(args[0], "down") == 0) {
            direction = MWC_DOWN;
        } else if(strcmp(args[0], "right") == 0) {
            direction = MWC_RIGHT;
        } else {
            wlr_log(WLR_ERROR, "invalid args to %s", action);
            free(k);
            return false;
        }

        k->action = keybind_swap_focused_toplevel;
        k->args = (void *)direction;
    } else if(strcmp(action, "workspace") == 0) {
        if(arg_count < 1) {
            wlr_log(WLR_ERROR, "invalid args to %s", action);
            free(k);
            return false;
        }
        k->action = keybind_change_workspace;
        /* this is going to be overriden by the actual workspace that is needed for change_workspace() */
        k->args = (void *)atoi(args[0]);
        k->initialized = false;
    } else if(strcmp(action, "move_to_workspace") == 0) {
        if(arg_count < 1) {
            wlr_log(WLR_ERROR, "invalid args to %s", action);
            free(k);
            return false;
        }
        k->action = keybind_move_focused_toplevel_to_workspace;
        /* this is going to be overriden by the actual workspace that is needed for change_workspace() */
        k->args = (void *)atoi(args[0]);
        k->initialized = false;
    } else if(strcmp(action, "next_workspace") == 0) {
        k->action = keybind_next_workspace;
    } else if(strcmp(action, "prev_workspace") == 0) {
        k->action = keybind_prev_workspace;
    } else if(strcmp(action, "toggle_fullscreen") == 0) {
        k->action = keybind_focused_toplevel_toggle_fullscreen;
    } else {
        wlr_log(WLR_ERROR, "invalid keybind action %s", action);
        free(k);
        return false;
    }

    if(pointer) {
        wl_list_insert(&c->pointer_keybinds, &k->link);
    } else {
        wl_list_insert(&c->keybinds, &k->link);
    }
    return true;
}

static void
config_free_args(char **args, size_t arg_count) {
    for(size_t i = 0; i < arg_count; i++) {
        if(args[i] != NULL) free(args[i]);
    }

    free(args);
}

static bool
config_handle_value(struct mwc_config *c, char *keyword, char **args, size_t arg_count) {
    if(strcmp(keyword, "keyboard_rate") == 0) {
        if(arg_count < 1) goto invalid;

        c->keyboard_rate = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "keyboard_delay") == 0) {
        if(arg_count < 1) goto invalid;

        c->keyboard_delay = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "pointer_sensitivity") == 0) {
        if(arg_count < 1) goto invalid;

        c->pointer_sensitivity = clamp(atof(args[0]), -1.0, 1.0);
    } else if(strcmp(keyword, "pointer_acceleration") == 0) {
        if(arg_count < 1) {
            wlr_log(WLR_ERROR, "invalid args to %s", keyword);
            config_free_args(args, arg_count);
            return false;
        }
        c->pointer_acceleration =
            atoi(args[0]) ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
    } else if(strcmp(keyword, "pointer") == 0) {
        if(arg_count < 3) goto invalid;

        enum libinput_config_accel_profile accel =
            atoi(args[1]) ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;

        struct pointer_config *p = calloc(1, sizeof(*p));
        *p = (struct pointer_config){
            .name = strdup(args[0]),
            .acceleration = accel,
            .sensitivity = clamp(atof(args[2]), -1.0, 1.0),
        };

        wl_list_insert(&c->pointers, &p->link);
    } else if(strcmp(keyword, "pointer_left_handed") == 0) {
        if(arg_count < 1) goto invalid;

        c->pointer_left_handed = atoi(args[0]);
    } else if(strcmp(keyword, "trackpad_disable_while_typing") == 0) {
        if(arg_count < 1) goto invalid;

        c->trackpad_disable_while_typing = atoi(args[0]);
    } else if(strcmp(keyword, "natural_scroll") == 0  // for backwards compatibility
        || strcmp(keyword, "trackpad_natural_scroll") == 0) {
        if(arg_count < 1) goto invalid;

        c->trackpad_natural_scroll = atoi(args[0]);
    } else if(strcmp(keyword, "tap_to_click") == 0  // for backwards compatibility
        || strcmp(keyword, "trackpad_tap_to_click") == 0) {
        if(arg_count < 1) goto invalid;

        c->trackpad_tap_to_click = atoi(args[0]);
    } else if(strcmp(keyword, "trackpad_scroll_method") == 0) {
        if(arg_count < 1) goto invalid;

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
    } else if(strcmp(keyword, "border_width") == 0) {
        if(arg_count < 1) goto invalid;

        c->border_width = clamp(atoi(args[0]), 0, INT_MAX);
    } else if(strcmp(keyword, "outer_gaps") == 0) {
        if(arg_count < 1) goto invalid;

        c->outer_gaps = clamp(atoi(args[0]), 0, INT_MAX);
    } else if(strcmp(keyword, "inner_gaps") == 0) {
        if(arg_count < 1) goto invalid;

        c->inner_gaps = clamp(atoi(args[0]), 0, INT_MAX);
    } else if(strcmp(keyword, "master_ratio") == 0) {
        if(arg_count < 1) goto invalid;

        c->master_ratio = clamp(atof(args[0]), 0, 1);
    } else if(strcmp(keyword, "master_count") == 0) {
        if(arg_count < 1) goto invalid;

        c->master_count = clamp(atoi(args[0]), 1, INT_MAX);
    } else if(strcmp(keyword, "cursor_theme") == 0) {
        if(arg_count < 1) goto invalid;

        c->cursor_theme = strdup(args[0]);
    } else if(strcmp(keyword, "cursor_size") == 0) {
        if(arg_count < 1) goto invalid;

        c->cursor_size = clamp(atoi(args[0]), 0, INT_MAX);
    } else if(strcmp(keyword, "inactive_border_color") == 0) {
        if(!try_parse_color_rgba_or_hex(args, arg_count, &c->inactive_border_color)) {
            goto invalid;
        }
    } else if(strcmp(keyword, "active_border_color") == 0) {
        if(!try_parse_color_rgba_or_hex(args, arg_count, &c->active_border_color)) {
            goto invalid;
        }
    } else if(strcmp(keyword, "output") == 0) {
        if(arg_count < 6) goto invalid;

        struct output_config *m = calloc(1, sizeof(*m));
        *m = (struct output_config){
            .name = strdup(args[0]),
            .x = atoi(args[1]),
            .y = atoi(args[2]),
            .width = atoi(args[3]),
            .height = atoi(args[4]),
            .refresh_rate = atoi(args[5]) * 1000,
            // scale is optional, defaults to 1
            .scale = arg_count > 6 ? atof(args[6]) : 1,
        };

        wl_list_insert(&c->outputs, &m->link);
    } else if(strcmp(keyword, "workspace") == 0) {
        if(arg_count < 2) goto invalid;

        struct workspace_config *w = calloc(1, sizeof(*w));
        *w = (struct workspace_config){
            .index = atoi(args[0]),
            .output = strdup(args[1]),
        };

        wl_list_insert(&c->workspaces, &w->link);
    } else if(strcmp(keyword, "run") == 0) {
        if(arg_count < 1) goto invalid;

        if(c->run_count > 64) {
            wlr_log(WLR_ERROR, "do you really need 65 runs?");
            return false;
        }
        c->run[c->run_count] = strdup(args[0]);
        c->run_count++;
    } else if(strcmp(keyword, "keybind") == 0) {
        if(arg_count < 3) goto invalid;

        config_add_keybind(c, args[0], args[1], args[2], &args[3], arg_count - 3);
    } else if(strcmp(keyword, "env") == 0) {
        if(arg_count < 2) goto invalid;

        setenv(args[0], args[1], true);
    } else if(strcmp(keyword, "window_rule") == 0) {
        if(arg_count < 3) goto invalid;

        config_add_window_rule(c, args[0], args[1], args[2], &args[3], arg_count - 3);
    } else if(strcmp(keyword, "animations") == 0) {
        if(arg_count < 1) goto invalid;

        c->animations = atoi(args[0]);
    } else if(strcmp(keyword, "animation_duration") == 0) {
        if(arg_count < 1) goto invalid;

        c->animation_duration = clamp(atoi(args[0]), 0, INT_MAX);
    } else if(strcmp(keyword, "animation_curve") == 0) {
        if(arg_count < 4) goto invalid;

        c->animation_curve =
            fx_animation_curve_create((double[4]){atof(args[0]), atof(args[1]), atof(args[2]), atof(args[3])});
    } else if(strcmp(keyword, "client_side_decorations") == 0) {
        if(arg_count < 1) goto invalid;

        if(atoi(args[0]) != 0) {
            c->decorations = MWC_DECORATIONS_CLIENT_SIDE;
        }
    } else if(strcmp(keyword, "inactive_opacity") == 0) {
        if(arg_count < 1) goto invalid;

        c->inactive_opacity = clamp(atof(args[0]), 0.0, 1.0);
    } else if(strcmp(keyword, "active_opacity") == 0) {
        if(arg_count < 1) goto invalid;

        c->active_opacity = clamp(atof(args[0]), 0.0, 1.0);
    } else if(strcmp(keyword, "apply_opacity_when_fullscreen") == 0) {
        if(arg_count < 1) goto invalid;

        c->apply_opacity_when_fullscreen = atoi(args[0]);
    } else if(strcmp(keyword, "keymap") == 0) {
        if(arg_count < 2) goto invalid;
        // handle appending to this string
        config_add_keymap(c, args[0], args[1]);
    } else if(strcmp(keyword, "keymap_options") == 0) {
        if(arg_count < 1) goto invalid;

        c->keymap_options = strdup(args[0]);
    } else if(strcmp(keyword, "border_radius") == 0) {
        if(arg_count < 1) goto invalid;

        c->border_radius = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "border_radius_location") == 0) {
        if(arg_count < 1) goto invalid;

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
        if(arg_count < 1) goto invalid;

        c->blur = atoi(args[0]);
    } else if(strcmp(keyword, "blur_passes") == 0) {
        if(arg_count < 1) goto invalid;

        c->blur_params.num_passes = clamp(atoi(args[0]), 1, INT_MAX);
    } else if(strcmp(keyword, "blur_radius") == 0) {
        if(arg_count < 1) goto invalid;

        c->blur_params.radius = clamp(atoi(args[0]), 0, INT_MAX);
    } else if(strcmp(keyword, "blur_noise") == 0) {
        if(arg_count < 1) goto invalid;

        c->blur_params.noise = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "blur_brightness") == 0) {
        if(arg_count < 1) goto invalid;

        c->blur_params.brightness = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "blur_contrast") == 0) {
        if(arg_count < 1) goto invalid;

        c->blur_params.contrast = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "blur_saturation") == 0) {
        if(arg_count < 1) goto invalid;

        c->blur_params.saturation = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "shadows") == 0) {
        if(arg_count < 1) goto invalid;

        c->shadows = atoi(args[0]);
    } else if(strcmp(keyword, "shadows_size") == 0) {
        if(arg_count < 1) goto invalid;

        c->shadows_size = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "shadows_blur") == 0) {
        if(arg_count < 1) goto invalid;

        c->shadows_blur = max(atof(args[0]), 0.0);
    } else if(strcmp(keyword, "shadows_position") == 0) {
        if(arg_count < 2) goto invalid;

        c->shadows_position.x = atoi(args[0]);
        c->shadows_position.y = atoi(args[1]);
    } else if(strcmp(keyword, "shadows_color") == 0) {
        if(!try_parse_color_rgba_or_hex(args, arg_count, &c->shadows_color)) {
            goto invalid;
        }
    } else if(strcmp(keyword, "layer_rule") == 0) {
        if(arg_count < 2) goto invalid;

        config_add_layer_rule(c, args[0], args[1], &args[2], arg_count - 2);
    } else if(strcmp(keyword, "decorations") == 0) {
        if(arg_count < 1) goto invalid;

        if(strcmp(args[0], "client") == 0) {
            c->decorations = MWC_DECORATIONS_CLIENT_SIDE;
        } else if(strcmp(args[0], "server") == 0) {
            c->decorations = MWC_DECORATIONS_SERVER_SIDE;
        } else if(strcmp(args[0], "none") == 0) {
            c->decorations = MWC_DECORATIONS_NONE;
        }
    } else if(strcmp(keyword, "titlebar_height") == 0) {
        if(arg_count < 1) goto invalid;

        c->titlebar_height = max(atoi(args[0]), 0);
    } else if(strcmp(keyword, "titlebar_color") == 0) {
        if(arg_count < 1) goto invalid;

        if(!try_parse_color_hex(args[0], &c->titlebar_color_active)) {
            goto invalid;
        }

        if(arg_count == 1 || !try_parse_color_hex(args[1], &c->titlebar_color_inactive)) {
            c->titlebar_color_inactive = c->titlebar_color_active;
        }
    } else if(strcmp(keyword, "titlebar_include_close_button") == 0) {
        if(arg_count < 1) goto invalid;

        c->titlebar_include_close_button = atoi(args[0]);
    } else if(strcmp(keyword, "titlebar_close_button_size") == 0) {
        if(arg_count < 1) goto invalid;

        c->titlebar_close_button_size = atoi(args[0]);
    } else if(strcmp(keyword, "titlebar_close_button_position") == 0) {
        if(arg_count < 1) goto invalid;

        if(strcmp(args[0], "left") == 0) {
            c->titlebar_close_button_left = true;
        }
    } else if(strcmp(keyword, "titlebar_close_button_padding") == 0) {
        if(arg_count < 1) goto invalid;

        c->titlebar_close_button_padding_left = atoi(args[0]);
        c->titlebar_close_button_padding_right = arg_count > 1 ? atoi(args[1]) : c->titlebar_close_button_padding_left;
    } else if(strcmp(keyword, "titlebar_close_button_shape") == 0) {
        if(arg_count < 1) goto invalid;

        if(strcmp(args[0], "square") == 0) {
            c->titlebar_close_button_square = true;
        }
    } else if(strcmp(keyword, "titlebar_close_button_color") == 0) {
        if(arg_count < 1) goto invalid;

        if(!try_parse_color_hex(args[0], &c->titlebar_close_button_color_active)) {
            goto invalid;
        }

        if(arg_count == 1 || !try_parse_color_hex(args[1], &c->titlebar_close_button_color_inactive)) {
            c->titlebar_close_button_color_inactive = c->titlebar_close_button_color_active;
        }
    } else if(strcmp(keyword, "titlebar_include_title") == 0) {
        if(arg_count < 1) goto invalid;

        c->titlebar_include_title = atoi(args[0]);
    } else if(strcmp(keyword, "titlebar_center_title") == 0) {
        if(arg_count < 1) goto invalid;

        c->titlebar_center_title = atoi(args[0]);
    } else if(strcmp(keyword, "titlebar_title_padding") == 0) {
        if(arg_count < 1) goto invalid;

        c->titlebar_title_padding_left = atoi(args[0]);
        c->titlebar_title_padding_right = arg_count > 1 ? atoi(args[1]) : c->titlebar_title_padding_left;
    } else if(strcmp(keyword, "titlebar_title_color") == 0) {
        if(arg_count < 1) goto invalid;

        if(!try_parse_color_rgba_or_hex(args, arg_count, &c->titlebar_title_color)) {
            goto invalid;
        }
    } else if(strcmp(keyword, "titlebar_title_font") == 0) {
        if(arg_count < 1) goto invalid;

        c->font = fcft_from_name(1, (const char **)&args[0], NULL);
        if(c->font == NULL) {
            wlr_log(WLR_ERROR, "error while loading a font '%s', title wont be drawn", args[0]);
        }
    } else {
        wlr_log(WLR_ERROR, "invalid keyword %s", keyword);
        free(keyword);
        config_free_args(args, arg_count);
        return false;
    }

    free(keyword);
    config_free_args(args, arg_count);
    return true;

invalid:
    wlr_log(WLR_ERROR, "invalid args to %s", keyword);
    free(keyword);
    config_free_args(args, arg_count);
    return false;
}

static void
get_default_config_path(char *dest, size_t size) {
    char *default_config_path = getenv("MWC_DEFAULT_CONFIG_PATH");

    if(default_config_path == NULL) {
        default_config_path = "/usr/share/mwc/default.conf";
        wlr_log(WLR_INFO, "no env MWC_DEFAULT_CONFIG_PATH set, using the default %s", default_config_path);
    } else {
        wlr_log(WLR_INFO, "env MWC_DEFAULT_CONFIG_PATH set to %s, using it", default_config_path);
    }

    strncpy(dest, default_config_path, size);
}

static bool
get_config_path(char *dest, size_t size) {
    char *env_conf = getenv("MWC_CONFIG_PATH");
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

// assumes the line is newline teriminated, as it should be with fgets()
static bool
config_handle_line(char *line, size_t line_number, char **keyword, char ***args, size_t *args_count) {
    char *p = line;

    // skip whitespace
    while(*p == ' ' || *p == '\t')
        p++;

    // if its an empty line or it starts with '#' (comment) skip
    if(*p == '\n' || *p == '#') {
        return false;
    }

    size_t len = 0, cap = STRING_INITIAL_LENGTH;
    char *kw = calloc(cap, sizeof(char));
    size_t ars_len = 0, ars_cap = 8;
    char **ars = calloc(ars_cap, sizeof(*args));

    char *q = kw;
    while(*p != ' ' && *p != '\t' && *p != '\n') {
        if(len >= cap) {
            cap *= 2;
            keyword = realloc(keyword, cap);
            q = &kw[len];
        }
        *q = *p;
        p++;
        q++;
        len++;
    }

    if(len >= cap) {
        cap += 1;
        keyword = realloc(keyword, cap);
        q = &kw[len];
    }
    *q = 0;

    // skip whitespace
    while(*p == ' ' || *p == '\t')
        p++;

    if(*p == '\n') {
        wlr_log(WLR_ERROR, "config: line %zu: no args provided for %s", line_number, kw);
        return false;
    }

    while(*p != '\n') {
        if(ars_len >= ars_cap) {
            ars_cap *= 2;
            ars = realloc(ars, ars_cap * sizeof(*ars));
        }

        len = 0;
        cap = STRING_INITIAL_LENGTH;
        ars[ars_len] = calloc(cap, sizeof(char));

        q = ars[ars_len];
        bool word = false;
        if(*p == '\"') {
            word = true;
            p++;
        };

        while((word && *p != '\"' && *p != '\n') || (!word && *p != ' ' && *p != '\t' && *p != '\n')) {
            if(len >= cap) {
                cap *= 2;
                ars[ars_len] = realloc(ars[ars_len], cap);
                q = &ars[ars_len][len];
            }
            if(word && *p == '\\' && *(p + 1) == '\"') {
                *q = '\"';
                p += 2;
            } else if(word && *p == '\\' && *(p + 1) == '\\') {
                *q = '\\';
                p += 2;
            } else {
                *q = *p;
                p++;
            }
            q++;
            len++;
        }
        if(len >= cap) {
            cap += 1;
            ars[ars_len] = realloc(ars[ars_len], cap);
            q = &ars[ars_len][len];
        }
        *q = 0;
        ars_len++;

        if(word) p++;
        /* skip whitespace */
        while(*p == ' ' || *p == '\t')
            p++;
    }

    *args_count = ars_len;
    *keyword = kw;
    *args = ars;
    return true;
}

static void
config_set_default_needed_params(struct mwc_config *c) {
    // as we are initializing config with calloc, some fields that are necessary in order
    // for mwc to not crash may be not specified in the config.
    // we set their values to some default value.*/
    if(c->keyboard_rate == 0) {
        c->keyboard_rate = 150;
        wlr_log(WLR_INFO, "keyboard_rate not specified. using default %ud", c->keyboard_rate);
    }
    if(c->keyboard_delay == 0) {
        c->keyboard_delay = 50;
        wlr_log(WLR_INFO, "keyboard_delay not specified. using default %ud", c->keyboard_delay);
    }
    if(c->cursor_size == 0) {
        c->cursor_size = 24;
        wlr_log(WLR_INFO, "cursor_size not specified. using default %ud", c->cursor_size);
    }
    if(c->master_count == 0) {
        c->master_count = 1;
        wlr_log(WLR_INFO, "master_count not specified. using default %lf", c->master_ratio);
    }
    if(c->master_ratio == 0) {
        // here we evenly space toplevels if there is no master_ratio specified
        c->master_ratio = c->master_count / (double)(c->master_count + 1);
        wlr_log(WLR_INFO, "master_ratio not specified. using default %lf", c->master_ratio);
    }
    if(c->animations && c->animation_duration == 0) {
        c->animation_duration = 500;
        wlr_log(WLR_INFO, "animation_duration not specified. using default %ud", c->animation_duration);
    }
    if(c->animations && c->animation_curve == NULL) {
        c->animation_curve = fx_animation_curve_create((double[4]){0});
        wlr_log(WLR_INFO, "animation_curve not specified. using linear");
    }
    if(c->inactive_opacity == 0) {
        c->inactive_opacity = 1.0;
        wlr_log(WLR_INFO, "inactive_opacity not specified. using default %lf", c->inactive_opacity);
    }
    if(c->active_opacity == 0) {
        c->active_opacity = 1.0;
        wlr_log(WLR_INFO, "active_opacity not specified. using default %lf", c->active_opacity);
    }
    if(c->border_radius_location == 0) {
        c->border_radius_location = CORNER_LOCATION_ALL;
        wlr_log(WLR_INFO, "border_radius_location not specified. using all");
    }
    if(c->titlebar_close_button_size > c->titlebar_height) {
        c->titlebar_close_button_size = c->titlebar_height;
        wlr_log(WLR_INFO, "titlebar_close_button_size (%u) larger than titlebar_height (%u). setting it to %u",
            c->titlebar_close_button_size, c->titlebar_height, c->titlebar_close_button_size);
    }

    c->toplevel_minimum_needed_width = c->decorations == MWC_DECORATIONS_SERVER_SIDE && c->titlebar_include_close_button
        ? c->titlebar_close_button_size + c->titlebar_close_button_padding_left + c->titlebar_close_button_padding_right
        : 10;
}

extern struct mwc_server server;

struct mwc_config *
config_load() {
    struct mwc_config *c = calloc(1, sizeof(*c));

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

    wl_list_init(&c->keybinds);
    wl_list_init(&c->pointer_keybinds);
    wl_list_init(&c->outputs);
    wl_list_init(&c->workspaces);
    wl_list_init(&c->pointers);
    wl_list_init(&c->window_rules.floating);
    wl_list_init(&c->window_rules.size);
    wl_list_init(&c->window_rules.opacity);
    wl_list_init(&c->window_rules.no_titlebar);
    wl_list_init(&c->layer_rules.blur);

    /* you aint gonna have lines longer than 1kB */
    char line_buffer[1024] = {0};
    char *keyword, **args;
    size_t args_count;
    size_t line_number = 1;
    while(fgets(line_buffer, 1024, config_file) != NULL) {
        bool valid = config_handle_line(line_buffer, line_number, &keyword, &args, &args_count);
        if(valid) {
            config_handle_value(c, keyword, args, args_count);
        }
        line_number++;
    }

    fclose(config_file);
    config_set_default_needed_params(c);

    return c;
}

// workspaces are the only thing that are never freed, as we do not allow
// destroying them for the lifetime of the compositor
void
config_destroy(struct mwc_config *c) {
    free(c->dir);

    struct output_config *o, *o_temp;
    wl_list_for_each_safe(o, o_temp, &c->outputs, link) {
        free(o->name);
        free(o);
    }

    struct keybind *k, *k_temp;
    wl_list_for_each_safe(k, k_temp, &c->keybinds, link) {
        if(k->action == keybind_run) {
            free(k->args);
        }
        free(k);
    }
    wl_list_for_each_safe(k, k_temp, &c->pointer_keybinds, link) {
        free(k);
    }

    struct window_rule_float *wrf, *wrf_temp;
    wl_list_for_each_safe(wrf, wrf_temp, &c->window_rules.floating, link) {
        if(wrf->condition.has_app_id_regex) {
            regfree(&wrf->condition.app_id_regex);
        }
        if(wrf->condition.has_title_regex) {
            regfree(&wrf->condition.title_regex);
        }
        free(wrf);
    }
    struct window_rule_size *wrs, *wrs_temp;
    wl_list_for_each_safe(wrs, wrs_temp, &c->window_rules.size, link) {
        if(wrs->condition.has_app_id_regex) {
            regfree(&wrs->condition.app_id_regex);
        }
        if(wrs->condition.has_title_regex) {
            regfree(&wrs->condition.title_regex);
        }
        free(wrs);
    }
    struct window_rule_opacity *wro, *wro_temp;
    wl_list_for_each_safe(wro, wro_temp, &c->window_rules.opacity, link) {
        if(wro->condition.has_app_id_regex) {
            regfree(&wro->condition.app_id_regex);
        }
        if(wro->condition.has_title_regex) {
            regfree(&wro->condition.title_regex);
        }
        free(wro);
    }
    struct window_rule_no_titlebar *wrnt, *wrnt_temp;
    wl_list_for_each_safe(wrnt, wrnt_temp, &c->window_rules.no_titlebar, link) {
        if(wrnt->condition.has_app_id_regex) {
            regfree(&wrnt->condition.app_id_regex);
        }
        if(wrnt->condition.has_title_regex) {
            regfree(&wrnt->condition.title_regex);
        }
        free(wrnt);
    }

    struct layer_rule_blur *lrb, *lrb_temp;
    wl_list_for_each_safe(lrb, lrb_temp, &c->layer_rules.blur, link) {
        if(lrb->condition.has) {
            regfree(&lrb->condition.regex);
        }

        free(lrb);
    }

    free(c->keymap_layouts);
    free(c->keymap_variants);
    free(c->keymap_options);

    struct pointer_config *p, *p_temp;
    wl_list_for_each_safe(p, p_temp, &c->pointers, link) {
        free(p->name);
        free(p);
    }

    if(c->font != NULL) {
        fcft_destroy(c->font);
    }

    free(c->cursor_theme);

    fx_animation_curve_destroy(c->animation_curve);

    for(size_t i = 0; i < c->run_count; i++) {
        free(c->run[i]);
    }

    free(c);
}

static void
toplevel_reapply_effects_etc(struct mwc_toplevel *toplevel) {
    toplevel_recheck_opacity_rules(toplevel);

    if(toplevel->shadow != NULL) {
        wlr_scene_node_destroy(&toplevel->shadow->node);
        toplevel->shadow = NULL;
    }

    if(toplevel->border != NULL) {
        wlr_scene_node_destroy(&toplevel->border->node);
        toplevel->border = NULL;
    }

    if(toplevel->titlebar.tree != NULL) {
        wlr_scene_node_destroy(&toplevel->titlebar.tree->node);
        toplevel->titlebar.tree = NULL;
        toplevel->titlebar.base = NULL;
        toplevel->titlebar.close_button = NULL;
        toplevel->titlebar.title = NULL;
    }
}

static void
layout_reorganize(struct mwc_workspace *workspace) {
    uint32_t master_count = wl_list_length(&workspace->masters);

    if(master_count > server.config->master_count) {
        while(master_count > server.config->master_count) {
            struct wl_list *last = workspace->masters.prev;
            wl_list_remove(last);
            wl_list_insert(workspace->slaves.prev, last);
            master_count--;
        }

        return;
    }

    uint32_t slave_count = wl_list_length(&workspace->slaves);
    if(master_count < server.config->master_count && slave_count > 0) {
        while(master_count < server.config->master_count && slave_count > 0) {
            struct wl_list *last = workspace->slaves.prev;
            wl_list_remove(last);
            wl_list_insert(workspace->masters.prev, last);
            master_count++;
            slave_count--;
        }
    }
}

// fix this so that output configuration is extracted more regarding blur etc
void
config_reload() {
    struct mwc_config *c = config_load();
    if(c == NULL) {
        // if we couldnt load the config then skip the reload
        wlr_log(WLR_ERROR, "could not reload the config, keeping the old one");
        return;
    }

    // since we dont touch workspaces when reloading we destroy the old one and just patch it with old one
    // todo: this does not seem needed, so maybe just dont do it? keep for now
    struct workspace_config *wc, *wc_temp;
    wl_list_for_each_safe(wc, wc_temp, &c->workspaces, link) {
        free(wc->output);
        free(wc);
    }
    c->workspaces = server.config->workspaces;

    // replace the config, but keep the old one for some optimizations,
    // since we can just skip the values that havent changed
    struct mwc_config *old_config = server.config;
    server.config = c;

    // handle the reloading, first we reconfigure and reposition the outputs
    // todo: here we are very ineficient, walking the linked list for every output;
    // will be fixed by using a hashmap instead
    struct mwc_output *iter_output;
    wl_list_for_each(iter_output, &server.outputs, link) {
        struct output_config *output_config = output_find_config_by_name(iter_output->wlr_output->name);

        // todo: maybe try it this way, but cashed values should be better anyway,
        // this just reduces complexity

        // struct wlr_box output_box;
        // wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);
        //
        // if(o->width != output_box.width
        //     || o->height != output_box.height
        //     || abs((int32_t)o->refresh_rate - (int32_t)out->wlr_output->refresh) > 1000
        //     || o->scale != out->wlr_output->scale) {
        output_configure(iter_output->wlr_output, output_config);
        // }

        // if(o->x != output_box.x || o->y != output_box.y) {
        output_place_in_layout(iter_output, output_config);
        // }

        // todo: maybe move this down there with the toplevels
        layer_surfaces_configure(iter_output);
        output_configure_blur(iter_output);
    }

    struct mwc_keyboard *keyboard;
    wl_list_for_each(keyboard, &server.keyboards, link) {
        keyboard_configure(keyboard);
    }

    struct mwc_pointer *pointer;
    wl_list_for_each(pointer, &server.pointers, link) {
        pointer_configure(pointer);
    }

    wl_list_for_each(iter_output, &server.outputs, link) {
        struct mwc_workspace *iter_workspace;
        wl_list_for_each(iter_workspace, &iter_output->workspaces, link) {
            // we rewire the keybinds
            struct keybind *iter_keybind;
            wl_list_for_each(iter_keybind, &server.config->keybinds, link) {
                if(iter_keybind->action == keybind_change_workspace &&
                    (uintptr_t)iter_keybind->args == iter_workspace->index) {
                    iter_keybind->args = iter_workspace;
                    iter_keybind->initialized = true;
                } else if(iter_keybind->action == keybind_move_focused_toplevel_to_workspace &&
                    (uintptr_t)iter_keybind->args == iter_workspace->index) {
                    iter_keybind->args = iter_workspace;
                    iter_keybind->initialized = true;
                }
            }

            if(c->master_count != old_config->master_count) {
                layout_reorganize(iter_workspace);
            }

            struct mwc_toplevel *iter_toplevel;
            wl_list_for_each(iter_toplevel, &iter_workspace->masters, link) {
                toplevel_reapply_effects_etc(iter_toplevel);
            }
            wl_list_for_each(iter_toplevel, &iter_workspace->slaves, link) {
                toplevel_reapply_effects_etc(iter_toplevel);
            }
            wl_list_for_each(iter_toplevel, &iter_workspace->floating_toplevels, link) {
                toplevel_reapply_effects_etc(iter_toplevel);
            }

            layout_configure(iter_workspace);
        }

        struct mwc_layer_surface *iter_layer;
        for(size_t i = 0; i < 4; i++) {
            wl_list_for_each(iter_layer, &(&iter_output->layers.background)[i], link) {
                struct layer_rule_blur *b;
                bool found = false;
                wl_list_for_each(b, &server.config->layer_rules.blur, link) {
                    if(!b->condition.has ||
                        regexec(&b->condition.regex, iter_layer->wlr_layer_surface->namespace, 0, NULL, 0) == 0) {
                        wlr_scene_node_for_each_buffer(&iter_layer->scene->tree->node, iter_scene_buffer_apply_blur,
                            (void *)1);
                        found = true;
                        break;
                    }
                }

                if(!found) {
                    wlr_scene_node_for_each_buffer(&iter_layer->scene->tree->node, iter_scene_buffer_apply_blur,
                        (void *)0);
                }
            }
        }
    }

    wlr_server_decoration_manager_set_default_mode(server.kde_decoration_manager,
        c->decorations == MWC_DECORATIONS_CLIENT_SIDE ? WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT
                                                      : WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

    wlr_xcursor_manager_destroy(server.cursor_mgr);

    server.cursor_mgr = wlr_xcursor_manager_create(server.config->cursor_theme, server.config->cursor_size);
    char cursor_size[8];
    snprintf(cursor_size, sizeof(cursor_size), "%u", server.config->cursor_size);

    cursor_size[7] = 0;
    if(server.config->cursor_theme != NULL) {
        setenv("XCURSOR_THEME", server.config->cursor_theme, true);
    }
    setenv("XCURSOR_SIZE", cursor_size, true);

    config_destroy(old_config);
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

    if(dir == NULL) return NULL;

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
