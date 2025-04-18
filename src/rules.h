#pragma once

#include <regex.h>
#include <stdbool.h>
#include <wayland-util.h>

#include "toplevel.h"

struct window_rule_regex {
    bool has_app_id_regex;
    regex_t app_id_regex;
    bool has_title_regex;
    regex_t title_regex;
};

// basic window rule with no params; use for boolean actions
struct window_rule {
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

struct layer_rule_regex {
    bool has;
    regex_t regex;
};

struct layer_rule {
    struct layer_rule_regex condition;
    struct wl_list link;
};

bool
toplevel_matches_window_rule(struct mwc_toplevel *toplevel, struct window_rule_regex *condition);

bool
layer_surface_matches_layer_rule(struct mwc_layer_surface *layer_surface, struct layer_rule_regex *condition);

void
toplevel_recheck_window_rules(struct mwc_toplevel *toplevel);

void
layer_surface_check_rules(struct mwc_layer_surface *layer_surface);
