#include "rules.h"

#include "array.h"
#include "config.h"
#include "layer_surface.h"
#include "toplevel.h"

extern struct server server;

bool
toplevel_matches_toplevel_rule(struct toplevel *toplevel, struct toplevel_rule_regex *condition) {
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

// note: we go backwards when checking for rules, so the later rules 'override' the previous ones
static void
set_opacity(struct toplevel *toplevel) {
    for(struct toplevel_rule_opacity *iter = array_last(server.config->toplevel_rules.opacity);
            iter >= server.config->toplevel_rules.opacity; iter--) {
        if(toplevel_matches_toplevel_rule(toplevel, &iter->condition)) {
            toplevel->active_opacity = iter->active_value;
            toplevel->inactive_opacity = iter->inactive_value;
            return;
        }
    }

    toplevel->active_opacity = server.config->opacity.active;
    toplevel->inactive_opacity = server.config->opacity.inactive;
}

static void
set_blur(struct toplevel *toplevel) {
    for(struct toplevel_rule_bool *iter = array_last(server.config->toplevel_rules.blur);
            iter >= server.config->toplevel_rules.blur; iter--) {
        if(toplevel_matches_toplevel_rule(toplevel, &iter->condition)) {
            toplevel->has_blur = iter->value;
        }
    }

    toplevel->has_blur = server.config->blur;
}

static bool
should_have_shadow(struct toplevel *toplevel) {
    for(struct toplevel_rule_bool *iter = array_last(server.config->toplevel_rules.shadow);
            iter >= server.config->toplevel_rules.shadow; iter--) {
        if(toplevel_matches_toplevel_rule(toplevel, &iter->condition)) {
            return iter->value;
        }
    }

    return server.config->shadows;
}

static bool
should_have_border(struct toplevel *toplevel) {
    for(struct toplevel_rule_bool *iter = array_last(server.config->toplevel_rules.border);
            iter >= server.config->toplevel_rules.border; iter--) {
        if(toplevel_matches_toplevel_rule(toplevel, &iter->condition)) {
            return iter->value;
        }
    }

    return server.config->borders;
}

static bool
should_have_titlebar(struct toplevel *toplevel) {
    for(struct toplevel_rule_bool *iter = array_last(server.config->toplevel_rules.titlebar);
            iter >= server.config->toplevel_rules.titlebar; iter--) {
        if(toplevel_matches_toplevel_rule(toplevel, &iter->condition)) {
            return iter->value;
        }
    }

    return server.config->titlebars;
}

void
toplevel_check_rules(struct toplevel *toplevel) {
    set_opacity(toplevel);
    set_blur(toplevel);

    uint32_t types = 0;
    if(should_have_shadow(toplevel)) {
        types |= DECORATION_SHADOW;
    }
    if(should_have_border(toplevel)) {
        types |= DECORATION_BORDER;
    }
    if(should_have_titlebar(toplevel)) {
        types |= DECORATION_TITLEBAR;
    }
    decoration_set_types(&toplevel->decoration, types);

    decoration_set_blur(&toplevel->decoration, toplevel->has_blur, toplevel_should_have_optimized_blur(toplevel));
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
