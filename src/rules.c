#include "rules.h"

#include <assert.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include "array.h"
#include "config.h"
#include "layer_shell.h"
#include "mwc.h"
#include "toplevel.h"
#include "workspace.h"

extern struct server server;

static inline bool
satisfies_regex(char *s, regex_t *regex) {
    return s != NULL && regexec(regex, s, 0, NULL, 0) == 0;
}

static inline enum toplevel_mode_ext
get_extended_mode(struct toplevel *toplevel) {
    return server.mode == SERVER_MODE_MOVING && toplevel == server.grabbed_toplevel      ? TOPLEVEL_MODE_EXT_MOVING
            : server.mode == SERVER_MODE_RESIZING && toplevel == server.grabbed_toplevel ? TOPLEVEL_MODE_EXT_RESIZING
            : toplevel->mode == TOPLEVEL_MODE_FULLSCREEN                                 ? TOPLEVEL_MODE_EXT_FULLSCREEN
            : toplevel->mode == TOPLEVEL_MODE_FLOATING                                   ? TOPLEVEL_MODE_EXT_FLOATING
            : toplevel->mode == TOPLEVEL_MODE_MASTER                                     ? TOPLEVEL_MODE_EXT_MASTER
                                                                                         : TOPLEVEL_MODE_EXT_SLAVE;
}

bool
toplevel_matches_rule(struct toplevel *toplevel, struct toplevel_config *config) {
    if((config->specified & TOPLEVEL_FIELD_MATCH_FOCUSED) &&
            (toplevel == server.focused_toplevel) != !!config->is_focused)
        return false;

    if((config->specified & TOPLEVEL_FIELD_MATCH_FAKE_FULLSCREEN) &&
            (toplevel->is_fake_fullscreen) != !!config->is_fake_fullscreen)
        return false;

    enum toplevel_mode_ext mode = get_extended_mode(toplevel);
    if((config->specified & TOPLEVEL_FIELD_MATCH_MODE) && !(mode & config->mode))
        return false;

    if((config->specified & TOPLEVEL_FIELD_MATCH_APP_ID) &&
            !satisfies_regex(toplevel->xdg_toplevel->app_id, &config->app_id))
        return false;

    if((config->specified & TOPLEVEL_FIELD_MATCH_TITLE) &&
            !satisfies_regex(toplevel->xdg_toplevel->title, &config->title))
        return false;

    if(toplevel_is_tiled(toplevel)) {
        // we only check these layout related for tiled toplevels
        if((config->specified & TOPLEVEL_FIELD_MATCH_MASTER_COUNT) &&
                !matches_relation(config->master_relation, toplevel->workspace->master_count, config->master_count))
            return false;

        if((config->specified & TOPLEVEL_FIELD_MATCH_SLAVE_COUNT) &&
                !matches_relation(config->slave_relation, toplevel->workspace->slave_count, config->slave_count))
            return false;
    }

    return true;
}

// it is intentionally written like this so we can more easily expand it, dont judge
bool
layer_surface_matches_rule(struct layer_surface *layer_surface, struct layer_config *config) {
    if((config->specified & LAYER_FIELD_MATCH_NAMESPACE) &&
            !satisfies_regex(layer_surface->wlr_layer_surface->namespace, &config->namespace))
        return false;

    return true;
}
void
rules_update_for_toplevel(struct toplevel *toplevel) {
    uint32_t found = 0, types = 0;
    bool apply_opacity_to_decorations = false;
    for(struct toplevel_config *iter = array_last(server.config->toplevels); iter >= server.config->toplevels; iter--) {
        if(!toplevel_matches_rule(toplevel, iter))
            continue;

        if(!(found & TOPLEVEL_FIELD_CORNER_RADIUS) && (iter->specified & TOPLEVEL_FIELD_CORNER_RADIUS)) {
            toplevel->corner_radius = iter->corner_radius;
            found |= TOPLEVEL_FIELD_CORNER_RADIUS;
        }
        if(!(found & TOPLEVEL_FIELD_CORNER_LOCATION) && (iter->specified & TOPLEVEL_FIELD_CORNER_LOCATION)) {
            toplevel->corner_location = iter->corner_location;
            found |= TOPLEVEL_FIELD_CORNER_LOCATION;
        }
        if(!(found & TOPLEVEL_FIELD_OPACITY) && (iter->specified & TOPLEVEL_FIELD_OPACITY)) {
            toplevel->opacity = iter->opacity;
            found |= TOPLEVEL_FIELD_OPACITY;
        }
        if(!(found & TOPLEVEL_FIELD_APPLY_OPACITY_TO_DECORATIONS) &&
                (iter->specified & TOPLEVEL_FIELD_APPLY_OPACITY_TO_DECORATIONS)) {
            apply_opacity_to_decorations = iter->apply_opacity_to_decorations;
            found |= TOPLEVEL_FIELD_APPLY_OPACITY_TO_DECORATIONS;
        }
        if(!(found & TOPLEVEL_FIELD_CLIENT_SIDE_DECORATIONS) &&
                (iter->specified & TOPLEVEL_FIELD_CLIENT_SIDE_DECORATIONS)) {
            if(!!iter->client_side_decorations != !!toplevel->client_side_decorations) {
                // if this thing changes the mode
                toplevel->client_side_decorations = iter->client_side_decorations;
                if(toplevel->xdg_decoration != NULL) {
                    wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->xdg_decoration,
                            toplevel->client_side_decorations ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                                              : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
                }
            }
            found |= TOPLEVEL_FIELD_CLIENT_SIDE_DECORATIONS;
        }
        if(!(found & TOPLEVEL_FIELD_BLUR) && (iter->specified & TOPLEVEL_FIELD_BLUR)) {
            toplevel->blur = iter->blur;
            found |= TOPLEVEL_FIELD_BLUR;
        }
        if(!(found & TOPLEVEL_FIELD_SHADOW) && (iter->specified & TOPLEVEL_FIELD_SHADOW)) {
            if(iter->shadow) {
                types |= DECORATION_SHADOW;
            }
            found |= TOPLEVEL_FIELD_SHADOW;
        }
        if(!(found & TOPLEVEL_FIELD_BORDER) && (iter->specified & TOPLEVEL_FIELD_BORDER)) {
            if(iter->border) {
                types |= DECORATION_BORDER;
            }
            found |= TOPLEVEL_FIELD_BORDER;
        }
        if(!(found & TOPLEVEL_FIELD_TITLEBAR) && (iter->specified & TOPLEVEL_FIELD_TITLEBAR)) {
            if(iter->titlebar) {
                types |= DECORATION_TITLEBAR;
            }
            found |= TOPLEVEL_FIELD_TITLEBAR;
        }
        if(!(found & TOPLEVEL_FIELD_DEFAULT_SIZE) && (iter->specified & TOPLEVEL_FIELD_DEFAULT_SIZE)) {
            toplevel->default_width = iter->default_width;
            toplevel->default_height = iter->default_height;
            toplevel->width_is_relative = iter->width_is_relative;
            toplevel->height_is_relative = iter->height_is_relative;
            found |= TOPLEVEL_FIELD_DEFAULT_SIZE;
        }
        if(!(found & TOPLEVEL_FIELD_DEFAULT_POSITION) && (iter->specified & TOPLEVEL_FIELD_DEFAULT_POSITION)) {
            toplevel->default_position = iter->default_position;
            found |= TOPLEVEL_FIELD_DEFAULT_POSITION;
        }
    }

    // update the decorations
    decoration_set_blur(&toplevel->decoration, toplevel->blur);
    decoration_set_corner_radius(&toplevel->decoration, toplevel->corner_radius, toplevel->corner_location);
    if(apply_opacity_to_decorations) {
        decoration_set_opacity(&toplevel->decoration, toplevel->opacity);
    } else {
        decoration_set_opacity(&toplevel->decoration, 1.0);
    }
    decoration_set_types(&toplevel->decoration, types);
}

void
rules_update_for_layer_surface(struct layer_surface *layer_surface) {
    uint32_t found = 0;
    for(struct layer_config *iter = array_last(server.config->layers); iter >= server.config->layers; iter--) {
        if(!layer_surface_matches_rule(layer_surface, iter))
            continue;

        if(!(found & LAYER_FIELD_BLUR) && (iter->specified & LAYER_FIELD_BLUR)) {
            layer_surface->blur = iter->blur;
            found |= LAYER_FIELD_BLUR;
        }
        if(!(found & LAYER_FIELD_BLUR_IGNORE_TRANSPARENT) && (iter->specified & LAYER_FIELD_BLUR_IGNORE_TRANSPARENT)) {
            layer_surface->blur_ignore_transparent = iter->blur_ignore_transparent;
            found |= LAYER_FIELD_BLUR_IGNORE_TRANSPARENT;
        }
    }
}

// if(iter->relative_width) {
//     *width = toplevel->workspace->output->usable_area.width * iter->width / 100;
// } else {
//     *width = iter->width;
// }
//
// if(iter->relative_height) {
//     *height = toplevel->workspace->output->usable_area.height * iter->height / 100;
// } else {
//     *height = iter->height;
// }
//
// return true;

enum toplevel_default_mode
rules_get_toplevel_default_mode(struct toplevel *toplevel) {
    // we make toplevels float if they have fixed size or are children of another toplevel
    if((toplevel->xdg_toplevel->current.max_height &&
               toplevel->xdg_toplevel->current.max_height == toplevel->xdg_toplevel->current.min_height) ||
            (toplevel->xdg_toplevel->current.max_width &&
                    toplevel->xdg_toplevel->current.max_width == toplevel->xdg_toplevel->current.min_width) ||
            toplevel->xdg_toplevel->parent != NULL)
        return TOPLEVEL_DEFAULT_MODE_FLOATING;

    for(struct toplevel_config *iter = array_last(server.config->toplevels); iter >= server.config->toplevels; iter--) {
        if((iter->specified & TOPLEVEL_FIELD_DEFAULT_MODE) && toplevel_matches_rule(toplevel, iter))
            return iter->default_mode;
    }

    // since we always have the default config in place
    assert(false && "unreachable");
    return 0;
}
