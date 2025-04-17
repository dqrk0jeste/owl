#include "decoration.h"

#include <assert.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>

#include "config.h"
#include "mwc.h"
#include "text_node.h"
#include "toplevel.h"

static struct decoration_manager manager = {0};

static void
create_shadow(struct decoration *decoration) {
    assert(decoration->shadow == NULL);

    float wlr_color[4];
    mwc_color_to_wlr_color(manager.config->shadow_color, wlr_color);
    decoration->shadow = wlr_scene_shadow_create(decoration->tree, 0, 0, manager.config->border_radius,
            manager.config->shadow_blur, wlr_color);

    // we set the position here since its always going to be the same
    wlr_scene_node_set_position(&decoration->shadow->node, manager.config->shadow_position.x,
            manager.config->shadow_position.y);
}

static void
update_shadow(struct decoration *decoration, struct wlr_box *box) {
    assert(decoration->shadow != NULL);

    struct wlr_box shadow_box = {
            0,
            0,
            box->width + manager.config->shadow_size,
            box->height + manager.config->shadow_size,
    };

    // in shadow relative coords
    struct wlr_box relative_box = {
            -manager.config->shadow_position.x,
            -manager.config->shadow_position.y,
            box->width,
            box->height,
    };

    struct wlr_box intersection_box;
    wlr_box_intersection(&intersection_box, &relative_box, &shadow_box);

    uint32_t border_radius = max((int32_t)manager.config->border_radius - (int32_t)manager.config->border_width, 0);

    wlr_scene_shadow_set_size(decoration->shadow, shadow_box.width, shadow_box.height);
    wlr_scene_shadow_set_clipped_region(decoration->shadow,
            (struct clipped_region){
                    .area = intersection_box,
                    .corner_radius = border_radius,
                    .corners = manager.config->border_radius_location,
            });
}

static void
destroy_shadow(struct decoration *decoration) {
    assert(decoration->shadow != NULL);

    wlr_scene_node_destroy(&decoration->shadow->node);
    decoration->shadow = NULL;
}

static void
create_border(struct decoration *decoration) {
    assert(decoration->border == NULL);

    // its automatically placed at the container start
    decoration->border = wlr_scene_rect_create(decoration->tree, 0, 0, (float[4]){0});
    wlr_scene_rect_set_corner_radius(decoration->border, manager.config->border_radius,
            manager.config->border_radius_location);

    view_create_for_node(&decoration->border->node, MWC_BORDER, decoration->border);
}

static void
update_border(struct decoration *decoration, struct wlr_box *box) {
    assert(decoration->border != NULL);

    uint32_t border_width = manager.config->border_width;
    uint32_t border_radius = manager.config->border_radius;
    enum corner_location border_radius_location = manager.config->border_radius_location;

    wlr_scene_rect_set_size(decoration->border, box->width, box->height);

    box->x += border_width;
    box->y += border_width;
    box->width -= 2 * border_width;
    box->height -= 2 * border_width;

    wlr_scene_rect_set_clipped_region(decoration->border,
            (struct clipped_region){
                    .area = *box,
                    .corner_radius = max((int32_t)border_radius - (int32_t)border_width, 0),
                    .corners = border_radius_location,
            });
}

static void
destroy_border(struct decoration *decoration) {
    assert(decoration->border != NULL);

    wlr_scene_node_destroy(&decoration->border->node);
    decoration->border = NULL;
}

static void
create_titlebar(struct decoration *decoration) {
    assert(decoration->titlebar.tree == NULL);

    // create a new tree so we can work in titlebar relative coords
    // note: we dont place it anywhere since it depends on wheather there is a border or not
    decoration->titlebar.tree = wlr_scene_tree_create(decoration->tree);

    decoration->titlebar.base = wlr_scene_rect_create(decoration->titlebar.tree, 0, 0, (float[4]){0});
    wlr_scene_rect_set_corner_radius(decoration->titlebar.base,
            max((int32_t)manager.config->border_radius - (int32_t)manager.config->border_width, 0),
            CORNER_LOCATION_TOP & manager.config->border_radius_location);

    view_create_for_node(&decoration->titlebar.base->node, MWC_TITLEBAR_BASE, decoration->titlebar.base);

    if(manager.config->titlebar_include_close_button) {
        uint32_t size = manager.config->titlebar_close_button_size;
        decoration->titlebar.close_button = wlr_scene_rect_create(decoration->titlebar.tree, size, size, (float[4]){0});

        if(manager.config->titlebar_close_button_shape == TITLEBAR_CLOSE_BUTTON_SHAPE_CIRCLE) {
            wlr_scene_rect_set_corner_radius(decoration->titlebar.close_button, size / 2 + 1, CORNER_LOCATION_ALL);
        }

        view_create_for_node(&decoration->titlebar.close_button->node, MWC_TITLEBAR_CLOSE_BUTTON,
                decoration->titlebar.close_button);
    }

    if(manager.config->titlebar_include_title && manager.config->font != NULL) {
        decoration->titlebar.title = text_node_create(decoration->tree, NULL);

        view_create_for_node(&decoration->titlebar.title->scene_buffer->node, MWC_TITLEBAR_TITLE,
                decoration->titlebar.title);
    }
}

static void
update_titlebar(struct decoration *decoration, struct wlr_box *box) {
    assert(decoration->titlebar.tree != NULL);

    wlr_scene_node_set_position(&decoration->titlebar.tree->node, box->x, box->y);

    // set the size of the titlebar base
    wlr_scene_rect_set_size(decoration->titlebar.base, box->width, manager.config->titlebar_height);

    if(decoration->titlebar.close_button != NULL) {
        int32_t x = manager.config->titlebar_close_button_position == TITLEBAR_CLOSE_BUTTON_POSITION_LEFT
                ? manager.config->titlebar_close_button_padding.left
                : (int32_t)box->width - (int32_t)manager.config->titlebar_close_button_padding.right -
                        (int32_t)manager.config->titlebar_close_button_size;
        int32_t y =
                ((int32_t)manager.config->titlebar_height - (int32_t)manager.config->titlebar_close_button_size) / 2;

        wlr_scene_node_set_position(&decoration->titlebar.close_button->node, x, y);
    }

    if(decoration->titlebar.title != NULL) {
        uint32_t left_pad = manager.config->titlebar_title_padding.left;
        if(manager.config->titlebar_include_close_button &&
                manager.config->titlebar_close_button_position == TITLEBAR_CLOSE_BUTTON_POSITION_LEFT) {
            left_pad += manager.config->titlebar_close_button_padding.left +
                    manager.config->titlebar_close_button_size + manager.config->titlebar_close_button_padding.right;
        }

        int32_t x, y;
        if(manager.config->titlebar_center_title) {
            // we try to center it
            x = ((int32_t)box->width - (int32_t)decoration->titlebar.title->width) / 2;
            // but we dont want it placed before `left_pad`
            x = max(x, (int32_t)left_pad);
        } else {
            x = left_pad;
        }

        y = ((int32_t)manager.config->titlebar_height - (int32_t)decoration->titlebar.title->height) / 2;

        wlr_scene_node_set_position(&decoration->titlebar.title->scene_buffer->node, x, y);

        int32_t free_width = (int32_t)box->width - x - (int32_t)manager.config->titlebar_title_padding.right;
        if(manager.config->titlebar_include_close_button &&
                manager.config->titlebar_close_button_position == TITLEBAR_CLOSE_BUTTON_POSITION_RIGHT) {
            free_width -= (int32_t)manager.config->titlebar_close_button_size +
                    (int32_t)manager.config->titlebar_close_button_padding.left +
                    (int32_t)manager.config->titlebar_close_button_padding.right;
        }

        if(free_width <= 0) {
            wlr_scene_node_set_enabled(&decoration->titlebar.title->scene_buffer->node, false);
        } else {
            wlr_scene_node_set_enabled(&decoration->titlebar.title->scene_buffer->node, true);
            struct wlr_fbox box = {
                    .x = 0.0,
                    .y = 0.0,
                    .width = min(free_width, decoration->titlebar.title->width),
                    .height = decoration->titlebar.title->height,
            };
            wlr_scene_buffer_set_source_box(decoration->titlebar.title->scene_buffer, &box);
            wlr_scene_buffer_set_dest_size(decoration->titlebar.title->scene_buffer,
                    min(free_width, decoration->titlebar.title->width), decoration->titlebar.title->height);
        }
    }
}

static void
destroy_titlebar(struct decoration *decoration) {
    assert(decoration->titlebar.tree != NULL);

    // manually destroy this, see comment in `text_node_destroy()`
    if(decoration->titlebar.title != NULL) {
        text_node_destroy(decoration->titlebar.title);
    }

    wlr_scene_node_destroy(&decoration->titlebar.tree->node);
    decoration->titlebar.tree = NULL;
    decoration->titlebar.base = NULL;
    decoration->titlebar.close_button = NULL;
    decoration->titlebar.title = NULL;
}

static void
set_root_position(struct decoration *decoration) {
    int32_t x = -manager.config->border_width;
    int32_t y = -manager.config->border_width;
    if(decoration_has_titlebar(decoration)) {
        y -= manager.config->titlebar_height;
    }

    wlr_scene_node_set_position(&decoration->tree->node, x, y);
}

struct decoration *
decoration_create(struct wlr_scene_tree *parent, uint32_t types) {
    struct decoration *decoration = calloc(1, sizeof(*decoration));

    // add it to the managers list
    wl_list_insert(manager.decorations.prev, &decoration->link);

    decoration->types = types;

    decoration->tree = wlr_scene_tree_create(parent);
    wlr_scene_node_lower_to_bottom(&decoration->tree->node);
    set_root_position(decoration);

    // and create the wanted decorations
    if(decoration_has_shadow(decoration)) {
        create_shadow(decoration);
    }

    if(decoration_has_border(decoration)) {
        create_border(decoration);
    }

    if(decoration_has_titlebar(decoration)) {
        create_titlebar(decoration);
    }

    // we give it initial coloring
    decoration_set_active(decoration, false);

    return decoration;
}

void
decoration_destroy(struct decoration *decoration) {
    if(decoration_has_border(decoration)) {
        destroy_border(decoration);
    }

    if(decoration_has_shadow(decoration)) {
        destroy_shadow(decoration);
    }

    if(decoration_has_titlebar(decoration)) {
        destroy_titlebar(decoration);
    }

    wl_list_remove(&decoration->link);
    free(decoration);
}

void
decoration_set_types(struct decoration *decoration, uint32_t types) {
    // we compare to see what has changed and create/destroy if needed
    if((decoration->types & DECORATION_SHADOW) && !(types & DECORATION_SHADOW)) {
        destroy_shadow(decoration);
    } else if(!(decoration->types & DECORATION_SHADOW) && (types & DECORATION_SHADOW)) {
        create_shadow(decoration);
    }

    if((decoration->types & DECORATION_BORDER) && !(types & DECORATION_BORDER)) {
        destroy_border(decoration);
    } else if(!(decoration->types & DECORATION_BORDER) && (types & DECORATION_BORDER)) {
        create_border(decoration);
    }

    if((decoration->types & DECORATION_TITLEBAR) && !(types & DECORATION_TITLEBAR)) {
        destroy_titlebar(decoration);
    } else if(!(decoration->types & DECORATION_TITLEBAR) && (types & DECORATION_TITLEBAR)) {
        create_titlebar(decoration);
    }

    decoration->types = types;
    set_root_position(decoration);
    // and then configure them with the current size and state
    decoration_configure(decoration, decoration->width, decoration->height);
    decoration_set_active(decoration, decoration->active);
}

void
decoration_set_enabled(struct decoration *decoration, bool enabled) {
    wlr_scene_node_set_enabled(&decoration->tree->node, enabled);
}

void
decoration_set_active(struct decoration *decoration, bool active) {
    decoration->active = active;
    // we set things to their active/inactive colors
    if(decoration_has_border(decoration)) {
        float border_color[4];
        mwc_color_to_wlr_color(active ? manager.config->border_color.active : manager.config->border_color.inactive,
                border_color);
        wlr_scene_rect_set_color(decoration->border, border_color);
    }

    if(decoration_has_titlebar(decoration)) {
        float titlebar_color[4];
        mwc_color_to_wlr_color(active ? manager.config->titlebar_color.active : manager.config->titlebar_color.inactive,
                titlebar_color);
        wlr_scene_rect_set_color(decoration->titlebar.base, titlebar_color);

        if(decoration->titlebar.close_button != NULL) {
            mwc_color_to_wlr_color(active ? manager.config->titlebar_close_button_color.active
                                          : manager.config->titlebar_close_button_color.inactive,
                    titlebar_color);
            wlr_scene_rect_set_color(decoration->titlebar.close_button, titlebar_color);
        }
    }
}

void
decoration_configure(struct decoration *decoration, uint32_t width, uint32_t height) {
    decoration->width = width;
    decoration->height = height;

    struct wlr_box decoration_box = {0, 0, width, height};

    // update_* functions will crop the `decoration_box`, so we can pass them to the next one
    if(decoration_has_shadow(decoration)) {
        update_shadow(decoration, &decoration_box);
    }

    if(decoration_has_border(decoration)) {
        update_border(decoration, &decoration_box);
    }

    if(decoration_has_titlebar(decoration)) {
        update_titlebar(decoration, &decoration_box);
    }
}

struct wlr_box
decoration_get_content_box(struct decoration *decoration, struct wlr_box box) {
    if(decoration_has_border(decoration)) {
        box.x += manager.config->border_width;
        box.y += manager.config->border_width;
        box.width -= 2 * manager.config->border_width;
        box.height -= 2 * manager.config->border_width;
    }

    if(decoration_has_titlebar(decoration)) {
        box.y += manager.config->titlebar_height;
        box.height -= manager.config->titlebar_height;
    }

    if(box.width <= 0) box.width = 1;
    if(box.height <= 0) box.height = 1;

    return box;
}

void
decoration_manager_init(struct decoration_config *config) {
    manager.config = config;

    if(!manager.inited) {
        // if this it the first time than initialize the list and return
        wl_list_init(&manager.decorations);
        manager.inited = true;
        return;
    }

    // we update all the decorations to this new config
    struct decoration *iter;
    wl_list_for_each(iter, &manager.decorations, link) {
        if(decoration_has_shadow(iter)) {
            destroy_shadow(iter);
            create_shadow(iter);
        }

        if(decoration_has_border(iter)) {
            destroy_border(iter);
            create_border(iter);
        }

        if(decoration_has_titlebar(iter)) {
            destroy_titlebar(iter);
            create_titlebar(iter);
        }

        decoration_configure(iter, iter->width, iter->height);
        decoration_set_active(iter, iter->active);
    }
}

bool
decoration_is_enabled(struct decoration *decoration) {
    return decoration->tree->node.enabled;
}

bool
decoration_has_border(struct decoration *decoration) {
    return decoration->types & DECORATION_BORDER;
}

bool
decoration_has_shadow(struct decoration *decoration) {
    return decoration->types & DECORATION_SHADOW;
}

bool
decoration_has_titlebar(struct decoration *decoration) {
    return decoration->types & DECORATION_TITLEBAR;
}

extern struct mwc_server server;

void
server_handle_request_xdg_decoration(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

    wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
            server.config->client_side_decorations ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                                   : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}
