#include "rules.h"

#include "array.h"
#include "config.h"
#include "layer_surface.h"
#include "toplevel.h"

extern struct server server;

bool
toplevel_matches_window_rule(struct toplevel *toplevel, struct window_rule_regex *condition) {
    char *app_id = toplevel->xdg_toplevel->app_id;
    char *title = toplevel->xdg_toplevel->title;

    bool matches_app_id = !condition->has_app_id_regex ||
            (app_id != NULL && regexec(&condition->app_id_regex, app_id, 0, NULL, 0) == 0);

    bool matches_title =
            !condition->has_title_regex || (title != NULL && regexec(&condition->title_regex, title, 0, NULL, 0) == 0);

    return matches_app_id && matches_title;
}

bool
layer_surface_matches_layer_rule(struct layer_surface *layer_surface, struct layer_rule_regex *condition) {
    if(!condition->has)
        return true;

    char *namespace = layer_surface->wlr_layer_surface->namespace;
    return namespace != NULL && regexec(&condition->regex, namespace, 0, NULL, 0) == 0;
}

static void
check_opacity_rules(struct toplevel *toplevel) {
    for(struct window_rule_opacity *iter = server.config->window_rules.opacity;
            iter <= array_last(server.config->window_rules.opacity); iter++) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->active_opacity = iter->active_value;
            toplevel->inactive_opacity = iter->inactive_value;
            return;
        }
    }

    toplevel->active_opacity = server.config->opacity.active;
    toplevel->inactive_opacity = server.config->opacity.inactive;
}

static void
check_no_titlebar_rules(struct toplevel *toplevel) {
    // we only care about these rules if we are drawing the titlebars globally
    if(!server.config->titlebars) {
        toplevel->has_titlebar = false;
        return;
    }

    for(struct window_rule *iter = server.config->window_rules.no_titlebar;
            iter <= array_last(server.config->window_rules.no_titlebar); iter++) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->has_titlebar = false;
            return;
        }
    }

    toplevel->has_titlebar = true;
}

static void
check_no_border_rules(struct toplevel *toplevel) {
    // we only care about these rules if we are drawing the titlebars globally
    if(!server.config->borders) {
        toplevel->has_border = false;
        return;
    }

    for(struct window_rule *iter = server.config->window_rules.no_border;
            iter <= array_last(server.config->window_rules.no_border); iter++) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->has_border = false;
            return;
        }
    }

    toplevel->has_border = true;
}

static void
check_no_blur_rules(struct toplevel *toplevel) {
    // we only care about these rules if we are drawing the titlebars globally
    if(!server.config->blur) {
        toplevel->has_blur = false;
        return;
    }

    for(struct window_rule *iter = server.config->window_rules.no_blur;
            iter <= array_last(server.config->window_rules.no_blur); iter++) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->has_blur = false;
            return;
        }
    }

    toplevel->has_blur = true;
}

static void
check_no_shadow_rules(struct toplevel *toplevel) {
    // we only care about these rules if we are drawing the borders globally
    if(!server.config->shadows) {
        toplevel->has_shadow = false;
        return;
    }

    toplevel->has_shadow = true;

    for(struct window_rule *iter = server.config->window_rules.no_shadow;
            iter <= array_last(server.config->window_rules.no_shadow); iter++) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->has_shadow = false;
            return;
        }
    }

    toplevel->has_shadow = true;
}

void
toplevel_check_rules(struct toplevel *toplevel) {
    check_opacity_rules(toplevel);
    check_no_titlebar_rules(toplevel);
    check_no_shadow_rules(toplevel);
    check_no_border_rules(toplevel);
    check_no_blur_rules(toplevel);
}

static void
check_blur_rules(struct layer_surface *layer_surface) {
    for(struct layer_rule_blur *iter = server.config->layer_rules.blur;
            iter <= array_last(server.config->layer_rules.blur); iter++) {
        if(layer_surface_matches_layer_rule(layer_surface, &iter->condition)) {
            layer_surface->has_blur = true;
            layer_surface->blur_optimized = iter->optimized;
            layer_surface->blur_ignore_transparent = iter->ignore_transparent;
            return;
        }
    }

    layer_surface->has_blur = false;
}

void
layer_surface_check_rules(struct layer_surface *layer_surface) {
    // since all the rules affect blur we dont check any if blur is disabled globally
    if(!server.config->blur) {
        layer_surface->has_blur = false;
        return;
    }

    check_blur_rules(layer_surface);
}
