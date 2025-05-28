#include "decoration.h"

#include <assert.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>

#include "config.h"
#include "mwc.h"
#include "text_node.h"
#include "toplevel.h"

extern struct server server;

static void
create_shadow(struct decoration *decoration) {
    float wlr_color[4];
    color_to_wlr_color(server.config->shadow.color, wlr_color);
    decoration->shadow = wlr_scene_shadow_create(decoration->tree, 0, 0, 0, server.config->shadow.blur, wlr_color);

    // disable it initially
    wlr_scene_node_set_enabled(&decoration->shadow->node, false);

    // we set the position here since its always going to be the same
    wlr_scene_node_set_position(&decoration->shadow->node, server.config->shadow.x, server.config->shadow.y);
}

static void
update_shadow(struct decoration *decoration, struct wlr_box *box) {
    struct wlr_box shadow_box = {
            0,
            0,
            box->width + server.config->shadow.size,
            box->height + server.config->shadow.size,
    };

    // toplevel box in relative shadow coords
    struct wlr_box relative_box = {
            -server.config->shadow.x,
            -server.config->shadow.y,
            box->width,
            box->height,
    };

    struct wlr_box intersection_box;
    wlr_box_intersection(&intersection_box, &relative_box, &shadow_box);

    wlr_scene_shadow_set_size(decoration->shadow, shadow_box.width, shadow_box.height);
    wlr_scene_shadow_set_clipped_region(decoration->shadow,
            (struct clipped_region){
                    .area = intersection_box,
                    .corner_radius = decoration->corner_radius,
                    .corners = decoration->corner_location,
            });
}

static void
create_border(struct decoration *decoration) {
    // its automatically placed at the container start
    decoration->border = wlr_scene_rect_create(decoration->tree, 0, 0, (float[4]){0});
    view_create_for_node(&decoration->border->node, VIEW_BORDER, decoration->border);

    // disable it initially
    wlr_scene_node_set_enabled(&decoration->border->node, false);
}

static void
update_border(struct decoration *decoration, struct wlr_box *box) {
    int border_width = server.config->border.width;
    int corner_radius = decoration->corner_radius;
    enum corner_location corner_location = decoration->corner_location;

    wlr_scene_rect_set_size(decoration->border, box->width, box->height);

    box->x += border_width;
    box->y += border_width;
    box->width -= 2 * border_width;
    box->height -= 2 * border_width;

    wlr_scene_rect_set_clipped_region(decoration->border,
            (struct clipped_region){
                    .area = *box,
                    .corner_radius = max(corner_radius - border_width, 0),
                    .corners = corner_location,
            });
}

static void
create_titlebar(struct decoration *decoration) {
    // create a new tree so we can work in titlebar relative coords. note: we dont place it anywhere since it depends on
    // wheather there is a border or not
    decoration->titlebar.tree = wlr_scene_tree_create(decoration->tree);

    // disable it initially
    wlr_scene_node_set_enabled(&decoration->titlebar.tree->node, false);

    decoration->titlebar.base = wlr_scene_rect_create(decoration->titlebar.tree, 0, 0, (float[4]){0});
    view_create_for_node(&decoration->titlebar.base->node, VIEW_TITLEBAR_BASE, decoration->titlebar.base);

    if(server.config->titlebar.close_button.enabled) {
        int size = server.config->titlebar.close_button.size;
        decoration->titlebar.close_button = wlr_scene_rect_create(decoration->titlebar.tree, size, size, (float[4]){0});
        view_create_for_node(&decoration->titlebar.close_button->node, VIEW_TITLEBAR_CLOSE_BUTTON,
                decoration->titlebar.close_button);

        if(server.config->titlebar.close_button.shape == TITLEBAR_CLOSE_BUTTON_SHAPE_CIRCLE) {
            wlr_scene_rect_set_corner_radius(decoration->titlebar.close_button, size / 2 + 1, CORNER_LOCATION_ALL);
        }
    } else {
        // when recreating the decorations, this may be set to the previous pointer, which is now invalid
        decoration->titlebar.close_button = NULL;
    }

    if(server.config->titlebar.title.enabled && server.title_font != NULL) {
        decoration->titlebar.title = text_node_create(decoration->titlebar.tree, server.title_font, 1.0,
                server.config->titlebar.title.color, decoration->title);
        view_create_for_node(&decoration->titlebar.title->scene_buffer->node, VIEW_TITLEBAR_TITLE,
                decoration->titlebar.title);
    } else {
        // same as above
        decoration->titlebar.title = NULL;
    }
}

static void
update_titlebar(struct decoration *decoration, struct wlr_box *box) {
    wlr_scene_node_set_position(&decoration->titlebar.tree->node, box->x, box->y);

    // set the size of the titlebar base
    wlr_scene_rect_set_size(decoration->titlebar.base, box->width, server.config->titlebar.height);

    if(decoration->titlebar.close_button != NULL) {
        int x = server.config->titlebar.close_button.position == TITLEBAR_CLOSE_BUTTON_POSITION_LEFT
                ? server.config->titlebar.close_button.padding.left
                : box->width - server.config->titlebar.close_button.padding.right -
                        server.config->titlebar.close_button.size;
        int y = (server.config->titlebar.height - server.config->titlebar.close_button.size) / 2;

        wlr_scene_node_set_position(&decoration->titlebar.close_button->node, x, y);
    }

    if(decoration->titlebar.title != NULL) {
        int left_pad = server.config->titlebar.title.padding.left;
        if(server.config->titlebar.close_button.enabled &&
                server.config->titlebar.close_button.position == TITLEBAR_CLOSE_BUTTON_POSITION_LEFT) {
            left_pad += server.config->titlebar.close_button.padding.left + server.config->titlebar.close_button.size +
                    server.config->titlebar.close_button.padding.right;
        }

        int x = left_pad;
        // we try to center it, but we dont want it placed before `left_pad`
        if(server.config->titlebar.title.position == TITLEBAR_TITLE_POSITION_CENTER &&
                (box->width - decoration->titlebar.title->width) / 2 > left_pad) {
            x = (box->width - decoration->titlebar.title->width) / 2;
        }
        int y = (server.config->titlebar.height - decoration->titlebar.title->height) / 2;

        wlr_scene_node_set_position(&decoration->titlebar.title->scene_buffer->node, x, y);

        // int free_width = box->width - x - server.config->titlebar.title.padding.right;
        // if(server.config->titlebar.close_button.enabled &&
        //         server.config->titlebar.close_button.position == TITLEBAR_CLOSE_BUTTON_POSITION_RIGHT) {
        //     free_width -= server.config->titlebar.close_button.size +
        //             server.config->titlebar.close_button.padding.left +
        //             server.config->titlebar.close_button.padding.right;
        // }
        //
        // wlr_scene_node_set_enabled(&decoration->titlebar.title->scene_buffer->node, free_width > 0);
        // struct wlr_fbox clip_box = {
        //         .x = 0.0,
        //         .y = 0.0,
        //         .width = min(free_width, decoration->titlebar.title->width),
        //         .height = decoration->titlebar.title->height,
        // };
        // wlr_scene_buffer_set_source_box(decoration->titlebar.title->scene_buffer, &clip_box);
        // wlr_scene_buffer_set_dest_size(decoration->titlebar.title->scene_buffer,
        //         min(free_width, decoration->titlebar.title->width), server.config->titlebar.title.size);
    }
}

static void
set_min_size(struct decoration *decoration) {
    int width = 0, height = 0;

    if(decoration_has_border(decoration)) {
        width += 2 * server.config->border.width;
        height += 2 * server.config->border.width;
    }

    if(decoration_has_titlebar(decoration)) {
        height += server.config->titlebar.height;
        if(server.config->titlebar.close_button.enabled) {
            width += server.config->titlebar.close_button.size + server.config->titlebar.close_button.padding.left +
                    server.config->titlebar.close_button.padding.right;
        }
    }

    decoration->min_width = width;
    decoration->min_height = height;
}

void
decoration_init(struct decoration *decoration, struct wlr_scene_tree *content_tree) {
    // create a base tree for the decorations
    decoration->content_tree = content_tree;
    decoration->opacity = 1.0;
    decoration->tree = wlr_scene_tree_create(content_tree->node.parent);
    wlr_scene_node_lower_to_bottom(&decoration->tree->node);

    // create the decorations. note: they are going to be hidden until the first call to `decoration_set_types()`
    create_shadow(decoration);
    create_border(decoration);
    create_titlebar(decoration);
}

void
decoration_recreate(struct decoration *decoration) {
    wlr_scene_node_destroy(&decoration->titlebar.tree->node);
    wlr_scene_node_destroy(&decoration->border->node);
    wlr_scene_node_destroy(&decoration->shadow->node);

    create_shadow(decoration);
    create_border(decoration);
    create_titlebar(decoration);

    decoration->types = 0;
    wlr_scene_node_set_position(&decoration->content_tree->node, 0, 0);

    decoration_set_blur(decoration, decoration->blur);
    decoration_set_active(decoration, decoration->active);
    decoration_set_corner_radius(decoration, decoration->corner_radius, decoration->corner_location);
}

void
decoration_destroy(struct decoration *decoration) {
    wlr_scene_node_destroy(&decoration->tree->node);

    if(decoration->title != NULL) {
        free(decoration->title);
    }
}

static inline void
get_content_coords(struct decoration *decoration, int *x, int *y) {
    *x = *y = 0;

    if(decoration_has_border(decoration)) {
        *x += server.config->border.width;
        *y += server.config->border.width;
    }

    if(decoration_has_titlebar(decoration)) {
        *y += server.config->titlebar.height;
    }
}

static inline void
update_content_tree(struct decoration *decoration) {
    int x, y;
    get_content_coords(decoration, &x, &y);

    wlr_scene_node_set_position(&decoration->content_tree->node, x, y);
}

void
decoration_set_types(struct decoration *decoration, uint32_t types) {
    if(types == decoration->types)
        return;

    decoration->types = types;
    wlr_scene_node_set_enabled(&decoration->shadow->node, decoration_has_shadow(decoration));
    wlr_scene_node_set_enabled(&decoration->border->node, decoration_has_border(decoration));
    wlr_scene_node_set_enabled(&decoration->titlebar.tree->node, decoration_has_titlebar(decoration));

    // we update the content tree position
    update_content_tree(decoration);
    set_min_size(decoration);

    // and then configure them with the current decoration state
    decoration_configure(decoration, decoration->width, decoration->height);
    // we also need to update the corner radius, since the border might be turned off in this call, so titlebar border
    // should be changed
    decoration_set_corner_radius(decoration, decoration->corner_radius, decoration->corner_location);
}

void
decoration_set_active(struct decoration *decoration, bool active) {
    decoration->active = active;

    // we set things to their active/inactive colors
    float wlr_color[4];
    struct color color = active ? server.config->border.color.active : server.config->border.color.inactive;
    color.a *= decoration->opacity;
    color_premultiply(&color);

    color_to_wlr_color(color, wlr_color);
    wlr_scene_rect_set_color(decoration->border, wlr_color);

    color = active ? server.config->titlebar.color.active : server.config->titlebar.color.inactive;
    color.a *= decoration->opacity;
    color_premultiply(&color);

    color_to_wlr_color(color, wlr_color);
    wlr_scene_rect_set_color(decoration->titlebar.base, wlr_color);

    if(decoration->titlebar.close_button != NULL) {
        color = active ? server.config->titlebar.close_button.color.active
                       : server.config->titlebar.close_button.color.inactive;
        color.a *= decoration->opacity;
        color_premultiply(&color);

        color_to_wlr_color(color, wlr_color);
        wlr_scene_rect_set_color(decoration->titlebar.close_button, wlr_color);
    }
}

void
decoration_configure(struct decoration *decoration, int width, int height) {
    decoration->width = width;
    decoration->height = height;

    struct wlr_box decoration_box = {0, 0, width, height};

    // update_* functions will crop the `decoration_box`, so we can pass them to the next one. note: unlike other
    // functions, this one only updates the shown decorations, since this may be more computationally intensive,
    // like when resizing the toplevel
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

void
decoration_set_title(struct decoration *decoration, char *title) {
    // if the title is the same we do nothing
    if(decoration->title != NULL && strcmp(decoration->title, title) == 0)
        return;

    if(decoration->title != NULL) {
        free(decoration->title);
    }
    decoration->title = strdup(title);

    // if the title is there update it
    if(decoration->titlebar.title) {
        text_node_set_text(decoration->titlebar.title, title);
        decoration_configure(decoration, decoration->width, decoration->height);
    }
}

void
decoration_set_blur(struct decoration *decoration, enum blur blur) {
    decoration->blur = blur;

    wlr_scene_rect_set_backdrop_blur(decoration->border, blur != BLUR_NONE);
    wlr_scene_rect_set_backdrop_blur(decoration->titlebar.base, blur != BLUR_NONE);

    wlr_scene_rect_set_backdrop_blur_optimized(decoration->border, blur == BLUR_OPTIMIZED);
    wlr_scene_rect_set_backdrop_blur_optimized(decoration->titlebar.base, blur == BLUR_OPTIMIZED);
}

void
decoration_set_opacity(struct decoration *decoration, double opacity) {
    decoration->opacity = opacity;

    // this will repaint the colors
    decoration_set_active(decoration, decoration->active);

    if(decoration->titlebar.title != NULL) {
        wlr_scene_buffer_set_opacity(decoration->titlebar.title->scene_buffer, opacity);
    }
}

void
decoration_set_corner_radius(struct decoration *decoration, int corner_radius, enum corner_location corner_location) {
    decoration->corner_radius = corner_radius;
    decoration->corner_location = corner_location;

    wlr_scene_shadow_set_corner_radius(decoration->shadow, corner_radius);
    wlr_scene_rect_set_corner_radius(decoration->border, corner_radius, corner_location);
    wlr_scene_rect_set_corner_radius(decoration->titlebar.base,
            decoration_has_border(decoration) ? max(corner_radius - server.config->border.width, 0)
                                              : decoration->corner_radius,
            CORNER_LOCATION_TOP & decoration->corner_location);
}

void
decoration_get_decoration_size(struct decoration *decoration, int *width, int *height) {
    if(decoration_has_border(decoration)) {
        *width += 2 * server.config->border.width;
        *height += 2 * server.config->border.width;
    }

    if(decoration_has_titlebar(decoration)) {
        *height += server.config->titlebar.height;
    }
}

void
decoration_get_content_size(struct decoration *decoration, int *width, int *height) {
    if(decoration_has_border(decoration)) {
        *width -= 2 * server.config->border.width;
        *height -= 2 * server.config->border.width;
    }

    if(decoration_has_titlebar(decoration)) {
        *height -= server.config->titlebar.height;
    }

    *width = max(*width, 1);
    *height = max(*height, 1);
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
