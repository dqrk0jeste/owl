#include "config.h"
#include "keybinds.h"
#include "owl.h"

#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>

#define clamp(v, a, b) (max((a), min((v), (b))))

struct vec2
calculate_animation_curve_at(struct owl_config *c, double t) {
  struct vec2 point;

  point.x = 3 * t * (1 - t) * (1 - t) * c->animation_curve[0]
    + 3 * t * t * (1 - t) * c->animation_curve[2]
    + t * t * t;

  point.y = 3 * t * (1 - t) * (1 - t) * c->animation_curve[1]
    + 3 * t * t * (1 - t) * c->animation_curve[3]
    + t * t * t;

  return point;
}

/* thanks vaxry */
void bake_bezier_curve_points(struct owl_config *c) {
  c->baked_points = calloc(BAKED_POINTS_COUNT, sizeof(*c->baked_points));

  for(size_t i = 0; i < BAKED_POINTS_COUNT; i++) {
    c->baked_points[i] = calculate_animation_curve_at(c, (double)i / (BAKED_POINTS_COUNT - 1));
  }
}

bool
config_add_window_rule(struct owl_config *c, char *app_id_regex, char *title_regex,
                       char *predicate, char **args, size_t arg_count) {
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
      return false;
    }
    struct window_rule_size *window_rule = calloc(1, sizeof(*window_rule));
    window_rule->condition = condition;

    /* if it ends with '%' we treat it as a relative unit */
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
      return false;
    }
    struct window_rule_opacity *window_rule = calloc(1, sizeof(*window_rule));
    window_rule->condition = condition;

    window_rule->active_value = clamp(atof(args[0]), 0.0, 1.0);
    window_rule->inactive_value = arg_count > 1 ? clamp(atof(args[1]), 0.0, 1.0) : window_rule->active_value;

    wl_list_insert(&c->window_rules.opacity, &window_rule->link);
  }

  return true;
}

char *
string_append_with_comma(char *a, char *b, size_t *cap, bool comma) {
  /* append this while making sure there is enough space */
  size_t a_len = strlen(a);
  size_t b_len = strlen(b);
  while(*cap < a_len + b_len + 2) {
    *cap *= 2;
    a = realloc(a, *cap);
  }

  /* now there is enough space to fit the new one; we add , if its not the first one */
  if(comma) {
    a[a_len] = ',';
    a_len++;
  }
  /* and then copy the thing over */
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

void
config_add_keymap(struct owl_config *c, char *layout, char *variant) {
  /* everything here is ugly */
  static size_t layout_cap, variant_cap;
  static size_t count;
  if(c->keymap_layouts == 0) {
    /* it has not been allocated yet */
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

bool
config_add_keybind(struct owl_config *c, char *modifiers, char *key,
                   char* action, char **args, size_t arg_count) {
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
  bool mouse = false;
  if(strstr(key, "mouse_") == key) {
    mouse = true;
    key = key + strlen("mouse_");
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

  /* this is true for most, needs to be set to false if otherwise */
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
  } else if(strcmp(action, "switch_floating_state") == 0) {
    k->action = keybind_switch_focused_toplevel_state;
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

    enum owl_direction direction;
    if(strcmp(args[0], "up") == 0) {
      direction = OWL_UP;
    } else if(strcmp(args[0], "left") == 0) {
      direction = OWL_LEFT;
    } else if(strcmp(args[0], "down") == 0) {
      direction = OWL_DOWN;
    } else if(strcmp(args[0], "right") == 0) {
      direction = OWL_RIGHT;
    } else {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    k->action = keybind_move_focus;
    k->args = (void*)direction;
  } else if(strcmp(action, "swap") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    enum owl_direction direction;
    if(strcmp(args[0], "up") == 0) {
      direction = OWL_UP;
    } else if(strcmp(args[0], "left") == 0) {
      direction = OWL_LEFT;
    } else if(strcmp(args[0], "down") == 0) {
      direction = OWL_DOWN;
    } else if(strcmp(args[0], "right") == 0) {
      direction = OWL_RIGHT;
    } else {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }

    k->action = keybind_swap_focused_toplevel;
    k->args = (void*)direction;
  } else if(strcmp(action, "workspace") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }
    k->action = keybind_change_workspace;
    /* this is going to be overriden by the actual workspace that is needed for change_workspace() */
    k->args = (void*)atoi(args[0]);
    k->initialized = false;
  } else if(strcmp(action, "move_to_workspace") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", action);
      free(k);
      return false;
    }
    k->action = keybind_move_focused_toplevel_to_workspace;
    /* this is going to be overriden by the actual workspace that is needed for change_workspace() */
    k->args = (void*)atoi(args[0]);
    k->initialized = false;
  } else if(strcmp(action, "next_workspace") == 0) {
    k->action = keybind_next_workspace;
  } else if(strcmp(action, "prev_workspace") == 0) {
    k->action = keybind_prev_workspace;
  } else {
    wlr_log(WLR_ERROR, "invalid keybind action %s", action);
    free(k);
    return false;
  }

  if(mouse) {
    wl_list_insert(&c->mouse_keybinds, &k->link);
  } else {
    wl_list_insert(&c->keybinds, &k->link);
  }
  return true;
}

void
config_free_args(char **args, size_t arg_count) {
  for(size_t i = 0; i < arg_count; i++) {
    if(args[i] != NULL) free(args[i]);
  }
}

bool
config_handle_value(struct owl_config *c, char *keyword, char **args, size_t arg_count) {
  if(strcmp(keyword, "min_toplevel_size") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->min_toplevel_size = clamp(atoi(args[0]), 0, INT_MAX);
  } else if(strcmp(keyword, "keyboard_rate") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->keyboard_rate = clamp(atoi(args[0]), 0, INT_MAX);
  } else if(strcmp(keyword, "keyboard_delay") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->keyboard_delay = clamp(atoi(args[0]), 0, INT_MAX);
  } else if(strcmp(keyword, "natural_scroll") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->natural_scroll = atoi(args[0]);
  } else if(strcmp(keyword, "tap_to_click") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->tap_to_click = atoi(args[0]);
  } else if(strcmp(keyword, "border_width") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->border_width = clamp(atoi(args[0]), 0, INT_MAX);
  } else if(strcmp(keyword, "outer_gaps") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->outer_gaps = clamp(atoi(args[0]), 0, INT_MAX);
  } else if(strcmp(keyword, "inner_gaps") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->inner_gaps = clamp(atoi(args[0]), 0, INT_MAX);
  } else if(strcmp(keyword, "master_ratio") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->master_ratio = clamp(atof(args[0]), 0, 1);
  } else if(strcmp(keyword, "master_count") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->master_count = clamp(atoi(args[0]), 1, INT_MAX);
  } else if(strcmp(keyword, "cursor_theme") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->cursor_theme = strdup(args[0]);
  } else if(strcmp(keyword, "cursor_size") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->cursor_size = clamp(atoi(args[0]), 0, INT_MAX);
  } else if(strcmp(keyword, "inactive_border_color") == 0) {
    if(arg_count < 4) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->inactive_border_color[0] = clamp(atoi(args[0]), 0, 255) / 255.0;
    c->inactive_border_color[1] = clamp(atoi(args[1]), 0, 255) / 255.0;
    c->inactive_border_color[2] = clamp(atoi(args[2]), 0, 255) / 255.0;
    c->inactive_border_color[3] = clamp(atoi(args[3]), 0, 255) / 255.0;
  } else if(strcmp(keyword, "active_border_color") == 0) {
    if(arg_count < 4) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->active_border_color[0] = clamp(atoi(args[0]), 0, 255) / 255.0;
    c->active_border_color[1] = clamp(atoi(args[1]), 0, 255) / 255.0;
    c->active_border_color[2] = clamp(atoi(args[2]), 0, 255) / 255.0;
    c->active_border_color[3] = clamp(atoi(args[3]), 0, 255) / 255.0;
  } else if(strcmp(keyword, "output") == 0) {
    if(arg_count < 6) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    struct output_config *m = calloc(1, sizeof(*m));
    *m = (struct output_config){
      .name = strdup(args[0]),
      .x = atoi(args[1]),
      .y = atoi(args[2]),
      .width = atoi(args[3]),
      .height = atoi(args[4]),
      .refresh_rate = atoi(args[5]) * 1000,
    };
    wl_list_insert(&c->outputs, &m->link);
  } else if(strcmp(keyword, "workspace") == 0) {
    if(arg_count < 2) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    struct workspace_config *w = calloc(1, sizeof(*w));
    *w = (struct workspace_config){
      .index = atoi(args[0]),
      .output = strdup(args[1]),
    };
    wl_list_insert(&c->workspaces, &w->link);
  } else if(strcmp(keyword, "run") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    if(c->run_count >= 64) {
      wlr_log(WLR_ERROR, "do you really need 64 runs?");
      return false;
    }
    c->run[c->run_count] = strdup(args[0]);
    c->run_count++;
  } else if(strcmp(keyword, "keybind") == 0) {
    if(arg_count < 3) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    config_add_keybind(c, args[0], args[1], args[2], &args[3], arg_count - 3);
  } else if(strcmp(keyword, "env") == 0) {
    if(arg_count < 2) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    setenv(args[0], args[1], true);
  } else if(strcmp(keyword, "window_rule") == 0) {
    if(arg_count < 3) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    config_add_window_rule(c, args[0], args[1], args[2], &args[3], arg_count - 3);
  } else if(strcmp(keyword, "animations") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->animations = atoi(args[0]);
  } else if(strcmp(keyword, "animation_duration") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->animation_duration = clamp(atoi(args[0]), 0, INT_MAX);
  } else if(strcmp(keyword, "animation_curve") == 0) {
    if(arg_count < 4) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->animation_curve[0] = atof(args[0]);
    c->animation_curve[1] = atof(args[1]);
    c->animation_curve[2] = atof(args[2]);
    c->animation_curve[3] = atof(args[3]);
    bake_bezier_curve_points(c);
  } else if(strcmp(keyword, "placeholder_color") == 0) {
    if(arg_count < 4) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->placeholder_color[0] = clamp(atoi(args[0]), 0, 255) / 255.0;
    c->placeholder_color[1] = clamp(atoi(args[1]), 0, 255) / 255.0;
    c->placeholder_color[2] = clamp(atoi(args[2]), 0, 255) / 255.0;
    c->placeholder_color[3] = clamp(atoi(args[3]), 0, 255) / 255.0;
  } else if(strcmp(keyword, "client_side_decorations") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->client_side_decorations = atoi(args[0]);
  } else if(strcmp(keyword, "inactive_opacity") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->inactive_opacity = clamp(atof(args[0]), 0.0, 1.0);
  } else if(strcmp(keyword, "active_opacity") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->active_opacity = clamp(atof(args[0]), 0.0, 1.0);
  } else if(strcmp(keyword, "keymap") == 0) {
    if(arg_count < 2) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    /* handle appending to this string */
    config_add_keymap(c, args[0], args[1]);
  } else if(strcmp(keyword, "keymap_options") == 0) {
    if(arg_count < 1) {
      wlr_log(WLR_ERROR, "invalid args to %s", keyword);
      config_free_args(args, arg_count);
      return false;
    }
    c->keymap_options = strdup(args[0]);
  } else {
    wlr_log(WLR_ERROR, "invalid keyword %s", keyword);
    config_free_args(args, arg_count);
    return false;
  }

  config_free_args(args, arg_count);
  return true;
}

FILE *
try_open_config_file() {
  char path[512];
  char *config_home = getenv("XDG_CONFIG_HOME");
  if(config_home != NULL) {
    snprintf(path, sizeof(path), "%s/owl/owl.conf", config_home);
  } else {
    char *home = getenv("HOME");
    if(home != NULL) {
      snprintf(path, sizeof(path), "%s/.config/owl/owl.conf", home);
    } else {
      return NULL;
    }
  }

  return fopen(path, "r");
}

/* assumes the line is newline teriminated, as it should be with fgets() */
bool
config_handle_line(char *line, size_t line_number, char **keyword,
                   char ***args, size_t *args_count) {
  char *p = line;

  /* skip whitespace */
  while(*p == ' ') p++;

  /* if its an empty line or it starts with '#' (comment) skip */
  if(*p == '\n' || *p == '#') {
    return false; 
  }

  size_t len = 0, cap = STRING_INITIAL_LENGTH;
  char *kw = calloc(cap, sizeof(char));
  size_t ars_len = 0, ars_cap = 8;
  char **ars = calloc(ars_cap, sizeof(*args));

  char *q = kw;
  while(*p != ' ') {
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
  *q = 0;

  /* skip whitespace */
  while(*p == ' ') p++;

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

    while((word && *p != '\"' && *p != '\n') || (!word && *p != ' ' && *p != '\n')) {
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
    *q = 0;
    ars_len++;

    if(word) p++;
    /* skip whitespace */
    while(*p == ' ') p++;
  }

  *args_count = ars_len;
  *keyword = kw;
  *args = ars;
  return true;
}

void
config_set_default_needed_params(struct owl_config *c) {
  /* as we are initializing config with calloc, some fields that are necessary in order
   * for owl to not crash may be not specified in the config.
   * we set their values to some default value.*/
  if(c->keyboard_rate == 0) {
    c->keyboard_rate = 150;
    wlr_log(WLR_INFO,
            "keyboard_rate not specified. using default %ud", c->keyboard_rate);
  } 
  if(c->keyboard_delay == 0) {
    c->keyboard_delay = 50;
    wlr_log(WLR_INFO,
            "keyboard_delay not specified. using default %ud", c->keyboard_delay);
  }
  if(c->cursor_size == 0) {
    c->cursor_size = 24;
    wlr_log(WLR_INFO,
            "cursor_size not specified. using default %ud", c->cursor_size);
  }
  if(c->min_toplevel_size == 0) {
    c->min_toplevel_size = 10;
    wlr_log(WLR_INFO,
            "min_toplevel_size not specified. using default %ud", c->min_toplevel_size);
  }
  if(c->master_count == 0) {
    c->master_count = 1;
    wlr_log(WLR_INFO,
            "master_count not specified. using default %lf", c->master_ratio);
  }
  if(c->master_ratio == 0) {
    /* here we evenly space toplevels if there is no master_ratio specified */
    c->master_ratio = c->master_count / (double)(c->master_count + 1);
    wlr_log(WLR_INFO,
            "master_ratio not specified. using default %lf", c->master_ratio);
  }
  if(c->animations && c->animation_duration == 0) {
    c->animation_duration = 500;
    wlr_log(WLR_INFO,
            "animation_duration not specified. using default %ud", c->animation_duration);
  }
  if(c->animations && c->animation_curve[0] == 0 && c->animation_curve[1] == 0
     && c->animation_curve[2] == 0 && c->animation_curve[3] == 0) {
    bake_bezier_curve_points(c);
    wlr_log(WLR_INFO, "animation_curve not specified. baking default linear");
  }
  if(c->inactive_opacity == 0) {
    /* here we evenly space toplevels if there is no master_ratio specified */
    c->inactive_opacity = 1.0;
    wlr_log(WLR_INFO,
            "inactive_opacity not specified. using default %lf", c->inactive_opacity);
  }
  if(c->active_opacity == 0) {
    /* here we evenly space toplevels if there is no master_ratio specified */
    c->active_opacity = 1.0;
    wlr_log(WLR_INFO,
            "active_opacity not specified. using default %lf", c->active_opacity);
  }
}

extern struct owl_server server;

bool
server_load_config() {
  struct owl_config *c = calloc(1, sizeof(*c));

  FILE *config_file = try_open_config_file();
  if(config_file == NULL) {
    wlr_log(WLR_INFO, "couldn't open config file, backing to default config");
    char *default_config_path = getenv("OWL_DEFAULT_CONFIG_PATH");
    if(default_config_path == NULL) {
      default_config_path = "/usr/share/owl/default.conf";
      wlr_log(WLR_INFO, "no env OWL_DEFAULT_CONFIG_PATH set, using the default %s", default_config_path);
    } else {
      wlr_log(WLR_INFO, "env OWL_DEFAULT_CONFIG_PATH set to %s", default_config_path);
    }
    config_file = fopen(default_config_path, "r");
    if(config_file == NULL) {
      wlr_log(WLR_ERROR, "couldn't find the default config file");
      return false;
    } else {
      wlr_log(WLR_INFO, "using default config");
    }
  } else {
    wlr_log(WLR_INFO, "using custom config");
  }

  wl_list_init(&c->keybinds);
  wl_list_init(&c->mouse_keybinds);
  wl_list_init(&c->outputs);
  wl_list_init(&c->workspaces);
  wl_list_init(&c->window_rules.floating);
  wl_list_init(&c->window_rules.size);
  wl_list_init(&c->window_rules.opacity);

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

  server.config = c;
  return true;
}

