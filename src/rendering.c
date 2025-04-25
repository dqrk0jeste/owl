#include "rendering.h"

#include <assert.h>
#include <limits.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/fx/corner_location.h>
#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <wayland-util.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "config.h"
#include "layer_surface.h"
#include "mwc.h"
#include "text_node.h"
#include "toplevel.h"
#include "view.h"
#include "workspace.h"

extern struct server server;

struct iter_layer_apply_effects_args {
    bool has_blur, blur_ignore_transparent, blur_xray;
};

static void
iter_layer_apply_blur(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
    struct iter_layer_apply_effects_args *args = data;

    wlr_scene_buffer_set_backdrop_blur(buffer, args->has_blur);
    wlr_scene_buffer_set_backdrop_blur_optimized(buffer, args->blur_xray);
    wlr_scene_buffer_set_backdrop_blur_ignore_transparent(buffer, args->blur_ignore_transparent);
}

static void
layer_surface_apply_effects(struct layer_surface *layer_surface) {
    struct iter_layer_apply_effects_args args = {
            .has_blur = layer_surface->has_blur,
            .blur_ignore_transparent = layer_surface->blur_ignore_transparent,
            .blur_xray = layer_surface->blur_xray,
    };
    wlr_scene_node_for_each_buffer(&layer_surface->scene->tree->node, iter_layer_apply_blur, &args);
}

struct iter_toplevel_apply_effects_args {
    int32_t root_x, root_y;
    struct wlr_box geometry;
    uint32_t width, height;
    double width_scale, height_scale;
    double opacity;
    uint32_t border_radius;
    bool has_titlebar;
    bool has_blur, blur_xray;
    bool animating;
};

static void
iter_toplevel_apply_effects(struct wlr_scene_buffer *buffer, int lx, int ly, void *data) {
    struct iter_toplevel_apply_effects_args *args = data;

    wlr_scene_buffer_set_opacity(buffer, args->opacity);

    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
    if(scene_surface == NULL) return;

    struct wlr_surface *surface = scene_surface->surface;

    // stretch the buffer if needed. note: we also set the size to the desired size when not animating
    if(!args->animating || args->width_scale > 1 || args->height_scale > 1) {
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

    if(server.config->border_radius_location & CORNER_LOCATION_TOP_LEFT && !args->has_titlebar && x == 0 && y == 0) {
        corners |= CORNER_LOCATION_TOP_LEFT;
    }

    if(server.config->border_radius_location & CORNER_LOCATION_BOTTOM_LEFT && x == 0 &&
            y + surface->current.height == args->geometry.height) {
        corners |= CORNER_LOCATION_BOTTOM_LEFT;
    }

    if(server.config->border_radius_location & CORNER_LOCATION_TOP_RIGHT && !args->has_titlebar &&
            x + surface->current.width == args->geometry.width && y == 0) {
        corners |= CORNER_LOCATION_TOP_RIGHT;
    }

    if(server.config->border_radius_location & CORNER_LOCATION_BOTTOM_RIGHT &&
            x + surface->current.width == args->geometry.width &&
            y + surface->current.height == args->geometry.height) {
        corners |= CORNER_LOCATION_BOTTOM_RIGHT;
    }

    wlr_scene_buffer_set_corner_radius(buffer, args->border_radius, corners);

    // we dont blur subsurfaces
    if(wlr_subsurface_try_from_wlr_surface(surface) != NULL) return;

    wlr_scene_buffer_set_backdrop_blur(buffer, args->has_blur);
    wlr_scene_buffer_set_backdrop_blur_optimized(buffer, !args->blur_xray);
    wlr_scene_buffer_set_backdrop_blur_ignore_transparent(buffer, false);
}

static void
toplevel_apply_effects(struct toplevel *toplevel) {
    double opacity;
    if(!toplevel->fullscreen || server.config->opacity_apply_when_fullscreen) {
        opacity = toplevel == server.focused_toplevel ? toplevel->active_opacity : toplevel->inactive_opacity;
    } else {
        opacity = 1.0;
    }

    uint32_t border_radius = toplevel->fullscreen
            ? 0
            : max((int32_t)server.config->border_radius - (int32_t)server.config->border_width, 0);

    struct wlr_box geometry = toplevel_get_geometry(toplevel);
    struct wlr_box content_box = toplevel_get_current_display_content_box(toplevel);

    struct iter_toplevel_apply_effects_args args = {
            .root_x = toplevel->scene_tree->node.x,
            .root_y = toplevel->scene_tree->node.y,
            .geometry = geometry,
            .width = content_box.width,
            .height = content_box.height,
            .width_scale = (double)content_box.width / geometry.width,
            .height_scale = (double)content_box.height / geometry.height,
            .opacity = opacity,
            .border_radius = border_radius,
            .has_titlebar = decoration_has_titlebar(toplevel->decoration),
            .has_blur = toplevel->has_blur,
            .blur_xray = server.config->blur_xray && toplevel->floating,
            .animating = toplevel->animation != NULL,
    };
    wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node, iter_toplevel_apply_effects, &args);
}

static bool
toplevel_is_in_box(struct toplevel *toplevel, struct wlr_box *box) {
    struct wlr_box deco_box = toplevel_get_current_display_deco_box(toplevel);

    struct wlr_box dest;
    return wlr_box_intersection(&dest, &deco_box, box);
}

void
output_draw(struct output *output) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    if(output->active_workspace->fullscreen_toplevel != NULL) {
        // we only draw the fullscreen toplevel here
        // todo: optimize this more
        toplevel_apply_effects(output->active_workspace->fullscreen_toplevel);
        return;
    }

    // apply layer surface effects
    struct layer_surface *iter_layer_surface;
    for(size_t i = 0; i < 4; i++) {
        wl_list_for_each(iter_layer_surface, &(&output->layers.background)[i], link) {
            layer_surface_apply_effects(iter_layer_surface);
        }
    }

    if(server.grabbed_toplevel != NULL && toplevel_is_in_box(server.grabbed_toplevel, &output_box)) {
        toplevel_apply_effects(server.grabbed_toplevel);
    }

    struct output *iter_output;
    wl_list_for_each(iter_output, &server.outputs, link) {
        struct toplevel *iter_toplevel;
        wl_list_for_each(iter_toplevel, &iter_output->active_workspace->masters, link) {
            if(toplevel_is_in_box(iter_toplevel, &output_box)) {
                toplevel_apply_effects(iter_toplevel);
            }
        }
        wl_list_for_each(iter_toplevel, &iter_output->active_workspace->slaves, link) {
            if(toplevel_is_in_box(iter_toplevel, &output_box)) {
                toplevel_apply_effects(iter_toplevel);
            }
        }
        wl_list_for_each(iter_toplevel, &iter_output->active_workspace->floating_toplevels, link) {
            if(toplevel_is_in_box(iter_toplevel, &output_box)) {
                toplevel_apply_effects(iter_toplevel);
            }
        }
    }
}
