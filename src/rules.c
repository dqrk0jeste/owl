#include "rules.h"

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
    if(!condition->has) return true;

    char *namespace = layer_surface->wlr_layer_surface->namespace;
    return namespace != NULL && regexec(&condition->regex, namespace, 0, NULL, 0) == 0;
}

static void
recheck_opacity_rules(struct toplevel *toplevel) {
    struct window_rule_opacity *iter;
    wl_list_for_each(iter, &server.config->window_rules.opacity, link) {
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
recheck_no_titlebar_rules(struct toplevel *toplevel) {
    // we only care about these rules if we are drawing the titlebars globally
    if(!server.config->titlebars) {
        toplevel->has_titlebar = false;
        return;
    }

    struct window_rule *iter;
    wl_list_for_each(iter, &server.config->window_rules.no_titlebar, link) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->has_titlebar = false;
            return;
        }
    }

    toplevel->has_titlebar = true;
}

static void
recheck_no_border_rules(struct toplevel *toplevel) {
    // we only care about these rules if we are drawing the titlebars globally
    if(!server.config->borders) {
        toplevel->has_border = false;
        return;
    }

    struct window_rule *iter;
    wl_list_for_each(iter, &server.config->window_rules.no_border, link) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->has_border = false;
            return;
        }
    }

    toplevel->has_border = true;
}

static void
recheck_no_blur_rules(struct toplevel *toplevel) {
    // we only care about these rules if we are drawing the titlebars globally
    if(!server.config->blur) {
        toplevel->has_blur = false;
        return;
    }

    struct window_rule *iter;
    wl_list_for_each(iter, &server.config->window_rules.no_blur, link) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->has_blur = false;
            return;
        }
    }

    toplevel->has_blur = true;
}

static void
recheck_no_shadow_rules(struct toplevel *toplevel) {
    // we only care about these rules if we are drawing the borders globally
    if(!server.config->shadows) {
        toplevel->has_shadow = false;
        return;
    }

    toplevel->has_shadow = true;

    struct window_rule *iter;
    wl_list_for_each(iter, &server.config->window_rules.no_shadow, link) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->has_shadow = false;
            return;
        }
    }

    toplevel->has_shadow = true;
}

void
toplevel_recheck_window_rules(struct toplevel *toplevel) {
    recheck_opacity_rules(toplevel);
    recheck_no_titlebar_rules(toplevel);
    recheck_no_shadow_rules(toplevel);
    recheck_no_border_rules(toplevel);
    recheck_no_blur_rules(toplevel);
}

static void
check_blur_rules(struct layer_surface *layer_surface) {
    struct layer_rule *iter;
    wl_list_for_each(iter, &server.config->layer_rules.blur, link) {
        if(layer_surface_matches_layer_rule(layer_surface, &iter->condition)) {
            layer_surface->has_blur = true;
            return;
        }
    }

    layer_surface->has_blur = false;
}

static void
check_blur_xray_rules(struct layer_surface *layer_surface) {
    struct layer_rule *iter;
    wl_list_for_each(iter, &server.config->layer_rules.blur_xray, link) {
        if(layer_surface_matches_layer_rule(layer_surface, &iter->condition)) {
            layer_surface->blur_xray = true;
            return;
        }
    }

    layer_surface->blur_xray = false;
}

static void
check_blur_ignore_transparent_rules(struct layer_surface *layer_surface) {
    struct layer_rule *iter;
    wl_list_for_each(iter, &server.config->layer_rules.blur_ignore_transparent, link) {
        if(layer_surface_matches_layer_rule(layer_surface, &iter->condition)) {
            layer_surface->blur_ignore_transparent = true;
            return;
        }
    }

    layer_surface->blur_ignore_transparent = false;
}

void
layer_surface_check_rules(struct layer_surface *layer_surface) {
    // since all the rules affect blur we dont check any if blur is disabled globally
    if(!server.config->blur) {
        layer_surface->has_blur = false;
        return;
    }

    check_blur_rules(layer_surface);
    check_blur_xray_rules(layer_surface);
    check_blur_ignore_transparent_rules(layer_surface);
}
