#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/fx/corner_location.h>
#include <scenefx/types/wlr_scene.h>

#include "rendering.h"

#include "helpers.h"
#include "mwc.h"
#include "config.h"
#include "something.h"
#include "toplevel.h"
#include "config.h"
#include "workspace.h"
#include "text_buffer.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <wayland-util.h>
#include <wlr/util/log.h>
#include <wlr/util/box.h>
#include <wlr/types/wlr_subcompositor.h>

extern struct mwc_server server;

static void
toplevel_create_titlebar(struct mwc_toplevel *toplevel, uint32_t width, uint32_t height) {
    assert(toplevel->titlebar.tree == NULL);

    toplevel->titlebar.tree = wlr_scene_tree_create(toplevel->scene_tree);
    toplevel->titlebar.tree->node.data = toplevel;

    toplevel->titlebar.base = wlr_scene_rect_create(toplevel->titlebar.tree, 0, 0, (float[4]){0});
    wlr_scene_node_lower_to_bottom(&toplevel->titlebar.base->node);
    wlr_scene_node_set_position(&toplevel->titlebar.tree->node, 0, -server.config->titlebar_height);
    wlr_scene_rect_set_corner_radius(toplevel->titlebar.base,
                                     max((int32_t)server.config->border_radius - (int32_t)server.config->border_width, 0),
                                     CORNER_LOCATION_TOP & server.config->border_radius_location);
    toplevel->titlebar.base_something.type = MWC_TITLEBAR_BASE;
    toplevel->titlebar.base_something.rect = toplevel->titlebar.base;

    if(server.config->blur) {
        wlr_scene_rect_set_backdrop_blur(toplevel->titlebar.base, true);
        wlr_scene_rect_set_backdrop_blur_optimized(toplevel->titlebar.base, true);
    }

    if(server.config->titlebar_include_close_button) {
        uint32_t size = server.config->titlebar_close_button_size;
        toplevel->titlebar.close_button = wlr_scene_rect_create(toplevel->titlebar.tree, size, size, (float[4]){0});

        if(!server.config->titlebar_close_button_square) {
            wlr_scene_rect_set_corner_radius(toplevel->titlebar.close_button, size / 2 + 1, CORNER_LOCATION_ALL);
        }

        toplevel->titlebar.close_button_something.type = MWC_TITLEBAR_CLOSE_BUTTON;
        toplevel->titlebar.close_button_something.rect = toplevel->titlebar.close_button;
    }

    if(server.config->titlebar_include_title && server.config->font != NULL) {
        toplevel->titlebar.title = text_node_create(toplevel->titlebar.tree, toplevel->xdg_toplevel->title);
    }

    wlr_scene_node_lower_to_bottom(&toplevel->titlebar.tree->node);
}

static void
toplevel_draw_titlebar(struct mwc_toplevel *toplevel) {
    if(toplevel->titlebar.tree != NULL && toplevel->fullscreen) {
        wlr_scene_node_set_enabled(&toplevel->titlebar.tree->node, false);
        return;
    }

    uint32_t width, height;
    toplevel_get_current_display_toplevel_size(toplevel, &width, &height);

    if(toplevel->titlebar.tree == NULL) {
        toplevel_create_titlebar(toplevel, width, height);
    }

    wlr_scene_node_set_enabled(&toplevel->titlebar.tree->node, true);
    wlr_scene_rect_set_size(toplevel->titlebar.base, width, server.config->titlebar_height);

    struct mwc_color color = toplevel == server.focused_toplevel
        ? server.config->titlebar_color_active
        : server.config->titlebar_color_inactive;

    float wlr_color[4];
    mwc_color_to_wlr_color(color, wlr_color);
    wlr_scene_rect_set_color(toplevel->titlebar.base, wlr_color);

    if(toplevel->titlebar.close_button != NULL) {
        color = toplevel == server.focused_toplevel
            ? server.config->titlebar_close_button_color_active
            : server.config->titlebar_close_button_color_inactive;

        mwc_color_to_wlr_color(color, wlr_color);
        wlr_scene_rect_set_color(toplevel->titlebar.close_button, wlr_color);

        int32_t x = server.config->titlebar_close_button_left
            ? server.config->titlebar_close_button_padding_left
            : (int32_t)width - (int32_t)server.config->titlebar_close_button_padding_right
            - (int32_t)server.config->titlebar_close_button_size;
        int32_t y = ((int32_t)server.config->titlebar_height - (int32_t)server.config->titlebar_close_button_size) / 2;

        wlr_scene_node_set_position(&toplevel->titlebar.close_button->node, x, y);
    }

    if(toplevel->titlebar.title != NULL) {
        uint32_t left_pad = server.config->titlebar_title_padding_left;
        if(server.config->titlebar_include_close_button && server.config->titlebar_close_button_left) {
            left_pad += server.config->titlebar_close_button_padding_left
                + server.config->titlebar_close_button_size
                + server.config->titlebar_close_button_padding_right;
        }

        int32_t x, y;
        if(server.config->titlebar_center_title) {
            x = max(((int32_t)width - (int32_t)toplevel->titlebar.title->width) / 2, (int32_t)left_pad);
        } else {
            x = left_pad;
        }

        y = ((int32_t)server.config->titlebar_height - (int32_t)toplevel->titlebar.title->height) / 2;

        wlr_scene_node_set_position(&toplevel->titlebar.title->scene_buffer->node, x, y);

        int32_t free_width = (int32_t)width - x - (int32_t)server.config->titlebar_title_padding_right;
        if(server.config->titlebar_include_close_button && !server.config->titlebar_close_button_left) {
            free_width -= (int32_t)server.config->titlebar_close_button_size
                + (int32_t)server.config->titlebar_close_button_padding_left
                + (int32_t)server.config->titlebar_close_button_padding_right;
        }

        if(free_width <= 0) {
            wlr_scene_node_set_enabled(&toplevel->titlebar.title->scene_buffer->node, false);
        } else {
            wlr_scene_node_set_enabled(&toplevel->titlebar.title->scene_buffer->node, true);
            const struct wlr_fbox box = {
                .x = 0.0,
                .y = 0.0,
                .width = min(free_width, toplevel->titlebar.title->width),
                .height = toplevel->titlebar.title->height,
            };
            wlr_scene_buffer_set_source_box(toplevel->titlebar.title->scene_buffer, &box);
            wlr_scene_buffer_set_dest_size(toplevel->titlebar.title->scene_buffer,
                                           min(free_width, toplevel->titlebar.title->width),
                                           toplevel->titlebar.title->height);
        }
    }
}

static void
toplevel_draw_border(struct mwc_toplevel *toplevel) {
    if(toplevel->border != NULL && toplevel->fullscreen) {
        wlr_scene_node_set_enabled(&toplevel->border->node, false);
        return;
    }

    uint32_t border_width = server.config->border_width;
    uint32_t border_radius = server.config->border_radius;
    enum corner_location border_radius_location = server.config->border_radius_location;

    if(toplevel->border == NULL) {
        toplevel->border = wlr_scene_rect_create(toplevel->scene_tree, 0, 0, (float[4]){0});
        wlr_scene_node_lower_to_bottom(&toplevel->border->node);

        // scene node relative coords of the container start
        int32_t x = -server.config->border_width;
        int32_t y = -server.config->border_width;
        if(toplevel->titlebar.has) {
            y -= server.config->titlebar_height;
        }
        wlr_scene_node_set_position(&toplevel->border->node, x, y);

        wlr_scene_rect_set_corner_radius(toplevel->border, border_radius, border_radius_location);
    }

    wlr_scene_node_set_enabled(&toplevel->border->node, true);

    struct wlr_box toplevel_box = toplevel_get_current_display_toplevel_box(toplevel);
    struct wlr_box container_box = toplevel_toplevel_box_to_container_box(toplevel_box, true, toplevel->titlebar.has);

    wlr_scene_rect_set_size(toplevel->border, container_box.width, container_box.height);

    uint32_t clipped_width = toplevel_box.width;
    uint32_t clipped_height = toplevel_box.height;
    if(toplevel->titlebar.has) {
        clipped_height += server.config->titlebar_height;
    }

    struct clipped_region clipped_region = {
        .area = { border_width, border_width, clipped_width, clipped_height },
        .corner_radius = max((int32_t)border_radius - (int32_t)border_width, 0),
        .corners = border_radius_location,
    };
    wlr_scene_rect_set_clipped_region(toplevel->border, clipped_region);

    struct mwc_color color = toplevel == server.focused_toplevel
        ? server.config->active_border_color
        : server.config->inactive_border_color;

    float wlr_color[4];
    mwc_color_to_wlr_color(color, wlr_color);
    wlr_scene_rect_set_color(toplevel->border, wlr_color);
}
//
// void
// toplevel_apply_clip(struct mwc_toplevel *toplevel) {
//     uint32_t width, height;
//     toplevel_get_current_display_toplevel_size(toplevel, &width, &height);
//
//     wlr_log(WLR_ERROR, "clip: %d, %d", width, height);
//
//     struct wlr_box geometry = toplevel_get_geometry(toplevel);
//     struct wlr_box clip_box = (struct wlr_box){
//         .x = geometry.x,
//         .y = geometry.y,
//         .width = width,
//         .height = height,
//     };
//
//     wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, &clip_box);
//
//     struct wlr_scene_node *n;
//     wl_list_for_each(n, &toplevel->scene_tree->children, link) {
//         struct mwc_something *view = n->data;
//         if(view != NULL && view->type == MWC_POPUP) {
//             wlr_scene_subsurface_tree_set_clip(n, NULL);
//         }
//     }
// }

static void
toplevel_draw_shadow(struct mwc_toplevel *toplevel) {
    if(toplevel->shadow != NULL && toplevel->fullscreen) {
        wlr_scene_node_set_enabled(&toplevel->shadow->node, false);
        return;
    }

    if(toplevel->shadow == NULL) {
        float wlr_color[4];
        mwc_color_to_wlr_color(server.config->shadows_color, wlr_color);
        toplevel->shadow = wlr_scene_shadow_create(toplevel->scene_tree,
                                                   0, 0,
                                                   server.config->border_radius,
                                                   server.config->shadows_blur,
                                                   wlr_color);
        wlr_scene_node_lower_to_bottom(&toplevel->shadow->node);

        // get the container start position
        int32_t x = -server.config->border_width;
        int32_t y = -server.config->border_width;
        if(toplevel->titlebar.has) {
            y -= server.config->titlebar_height;
        }
        // add the user specified position
        x += server.config->shadows_position.x;
        y += server.config->shadows_position.y;

        wlr_scene_node_set_position(&toplevel->shadow->node, x, y);
    }

    wlr_scene_node_set_enabled(&toplevel->shadow->node, true);

    struct wlr_box toplevel_box = toplevel_get_current_display_toplevel_box(toplevel);
    struct wlr_box container_box = toplevel_toplevel_box_to_container_box(toplevel_box,
                                                                          true, toplevel->titlebar.has);

    container_box.x = -server.config->shadows_position.x;
    container_box.y = -server.config->shadows_position.y;

    struct wlr_box shadow_box = {
        .x = 0,
        .y = 0,
        .width = container_box.width + server.config->shadows_size,
        .height = container_box.height + server.config->shadows_size,
    };

    struct wlr_box intersection_box;
    wlr_box_intersection(&intersection_box, &container_box, &shadow_box);

    wlr_scene_shadow_set_size(toplevel->shadow, shadow_box.width, shadow_box.height);
    wlr_scene_shadow_set_clipped_region(toplevel->shadow, (struct clipped_region){
        .area = intersection_box,
        .corner_radius = max((int32_t)server.config->border_radius - (int32_t)server.config->border_width, 0),
        .corners = server.config->border_radius_location,
    });
}

struct iter_scene_buffer_apply_effects_args {
    int32_t root_x;
    int32_t root_y;
    struct wlr_box geometry;
    uint32_t width;
    uint32_t height;
    double width_scale;
    double height_scale;
    double opacity;
    uint32_t border_radius;
    bool has_titlebar;
};

static void
iter_scene_buffer_apply_effects(struct wlr_scene_buffer *buffer, int lx, int ly, void *data) {
    struct iter_scene_buffer_apply_effects_args *args = data;

    wlr_scene_buffer_set_opacity(buffer, args->opacity);

    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
    if(scene_surface == NULL) return;

    struct wlr_surface *surface = scene_surface->surface;

    // stretch the buffer if needed
    if(args->width_scale > 1 || args->height_scale > 1) {
        uint32_t surface_width = surface->current.width;
        uint32_t surface_height = surface->current.height;

        surface_width *= args->width_scale;
        surface_height *= args->height_scale;

        wlr_scene_buffer_set_dest_size(buffer, surface_width, surface_height);
    }

    // we dont round or blur popups
    if(wlr_xdg_popup_try_from_wlr_surface(surface) != NULL) return;

    int32_t x = lx - args->root_x;
    int32_t y = ly - args->root_y;

    enum corner_location corners = 0;

    if(server.config->border_radius_location & CORNER_LOCATION_TOP_LEFT
        && !args->has_titlebar
        && x == 0
        && y == 0) {
        corners |= CORNER_LOCATION_TOP_LEFT;
    }

    if(server.config->border_radius_location & CORNER_LOCATION_BOTTOM_LEFT
        && x == 0
        && y + surface->current.height == args->geometry.height) {
        corners |= CORNER_LOCATION_BOTTOM_LEFT;
    }

    if(server.config->border_radius_location & CORNER_LOCATION_TOP_RIGHT
        && !args->has_titlebar
        && x + surface->current.width == args->geometry.width
        && y == 0) {
        corners |= CORNER_LOCATION_TOP_RIGHT;
    }

    if(server.config->border_radius_location & CORNER_LOCATION_BOTTOM_RIGHT
        && x + surface->current.width == args->geometry.width
        && y + surface->current.height == args->geometry.height) {
        corners |= CORNER_LOCATION_BOTTOM_RIGHT;
    }

    wlr_scene_buffer_set_corner_radius(buffer, args->border_radius, corners);

    // we dont blur subsurfaces
    if(wlr_subsurface_try_from_wlr_surface(surface) != NULL) return;

    if(server.config->blur) {
        wlr_scene_buffer_set_backdrop_blur(buffer, true);
        wlr_scene_buffer_set_backdrop_blur_optimized(buffer, true);
        wlr_scene_buffer_set_backdrop_blur_ignore_transparent(buffer, false);
    } else {
        wlr_scene_buffer_set_backdrop_blur(buffer, false);
    }
}

static void
toplevel_apply_effects(struct mwc_toplevel *toplevel) {
    double opacity;
    if(!toplevel->fullscreen || server.config->apply_opacity_when_fullscreen) {
        opacity = toplevel == server.focused_toplevel
            ? toplevel->active_opacity
            : toplevel->inactive_opacity;
    } else {
        opacity = 1.0;
    }

    uint32_t border_radius = toplevel->fullscreen
        ? 0
        : max(server.config->border_radius - server.config->border_width, 0);

    struct wlr_box geometry = toplevel_get_geometry(toplevel);

    uint32_t width, height;
    toplevel_get_current_display_toplevel_size(toplevel, &width, &height);

    struct iter_scene_buffer_apply_effects_args args = {
        .root_x = toplevel->scene_tree->node.x,
        .root_y = toplevel->scene_tree->node.y,
        .geometry = geometry,
        .width = width,
        .height = height,
        .width_scale = (double)width / geometry.width,
        .height_scale = (double)height / geometry.height,
        .opacity = opacity,
        .border_radius = border_radius,
        .has_titlebar = toplevel->titlebar.has,
    };

    wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
                                   iter_scene_buffer_apply_effects, &args);
}

static void
toplevel_draw(struct mwc_toplevel *toplevel) {
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);

    if(server.config->border_width > 0) {
        toplevel_draw_border(toplevel);
    }
    if(toplevel->titlebar.has) {
        toplevel_draw_titlebar(toplevel);
    }
    if(server.config->shadows) {
        toplevel_draw_shadow(toplevel);
    }
    toplevel_apply_effects(toplevel);
}

static bool
toplevel_is_in_box(struct mwc_toplevel *toplevel, struct wlr_box *box) {
    struct wlr_box toplevel_box = toplevel_get_current_display_toplevel_box(toplevel);

    struct wlr_box dest;
    return wlr_box_intersection(&dest, &toplevel_box, box);
}

void
output_draw(struct mwc_output *output) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    struct mwc_output *iter_output;
    wl_list_for_each(iter_output, &server.outputs, link) {
        struct mwc_toplevel *iter_toplevel;
        wl_list_for_each(iter_toplevel, &iter_output->active_workspace->masters, link) {
            if(toplevel_is_in_box(iter_toplevel, &output_box)) {
                toplevel_draw(iter_toplevel);
            }
        }
        wl_list_for_each(iter_toplevel, &iter_output->active_workspace->slaves, link) {
            if(toplevel_is_in_box(iter_toplevel, &output_box)) {
                toplevel_draw(iter_toplevel);
            }
        }
        wl_list_for_each(iter_toplevel, &iter_output->active_workspace->floating_toplevels, link) {
            if(toplevel_is_in_box(iter_toplevel, &output_box)) {
                toplevel_draw(iter_toplevel);
            }
        }
    }
}

