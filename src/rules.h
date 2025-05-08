#pragma once

#include <regex.h>
#include <stdbool.h>
#include <wayland-util.h>

#include "config.h"
#include "toplevel.h"

struct toplevel_rule_regex {
    bool has_app_id_regex;
    regex_t app_id_regex;
    bool has_title_regex;
    regex_t title_regex;
};

// basic toplevel rule with no params
struct toplevel_rule {
    struct toplevel_rule_regex condition;
};

struct toplevel_rule_bool {
    struct toplevel_rule_regex condition;
    bool value;
};

struct toplevel_rule_size {
    struct toplevel_rule_regex condition;
    bool relative_width;
    uint32_t width;
    bool relative_height;
    uint32_t height;
};

struct toplevel_rule_opacity {
    struct toplevel_rule_regex condition;
    double inactive_value;
    double active_value;
};

struct layer_rule_regex {
    bool has;
    regex_t regex;
};

struct layer_rule {
    struct layer_rule_regex condition;
};

struct layer_rule_blur {
    struct layer_rule_regex condition;
    enum blur_optimized optimized;
    bool ignore_transparent;
};

bool
toplevel_matches_toplevel_rule(struct toplevel *toplevel, struct toplevel_rule_regex *condition);

bool
layer_surface_matches_layer_rule(struct layer_surface *layer_surface, struct layer_rule_regex *condition);

// (re)check the toplevel rules for this toplevel
// note: this function will only update the flags, but you need to handle the updating of the actual presentation
// seperatelly, e.g. by calling decoration_set_types()
void
toplevel_check_rules(struct toplevel *toplevel);

void
layer_surface_check_rules(struct layer_surface *layer_surface);
