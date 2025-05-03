#include "layer_surface.h"

#include <assert.h>
#include <math.h>
#include <regex.h>
#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_scene.h>

#include "config.h"
#include "layout.h"
#include "mwc.h"
#include "output.h"
#include "popup.h"
#include "rules.h"
#include "toplevel.h"
#include "view.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "workspace.h"

extern struct server server;

static struct wlr_scene_tree *
layer_get_scene(enum zwlr_layer_shell_v1_layer layer) {
    switch(layer) {
        case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
            return server.background_tree;
        case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
            return server.bottom_tree;
        case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
            return server.top_tree;
        case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
            return server.overlay_tree;
    }
}

static struct wl_list *
layer_get_list(struct output *output, enum zwlr_layer_shell_v1_layer layer) {
    switch(layer) {
        case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
            return &output->layers.background;
        case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
            return &output->layers.bottom;
        case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
            return &output->layers.top;
        case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
            return &output->layers.overlay;
    }
}

static void
layer_surface_handle_commit(struct wl_listener *listener, void *data) {
    struct layer_surface *layer_surface = wl_container_of(listener, layer_surface, commit);

    if(!layer_surface->wlr_layer_surface->initialized)
        return;

    struct output *output = layer_surface->wlr_layer_surface->output->data;

    if(layer_surface->wlr_layer_surface->initial_commit) {
        // if its an initial commit we just send it a configure
        struct wlr_box output_box;
        wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);
        wlr_scene_layer_surface_v1_configure(layer_surface->scene, &output_box, &output->usable_area);
        return;
    }

    enum zwlr_layer_shell_v1_layer layer = layer_surface->wlr_layer_surface->current.layer;
    uint32_t committed = layer_surface->wlr_layer_surface->current.committed;
    if(committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
        // if the layer has been changed we respect it
        wl_list_remove(&layer_surface->link);
        wl_list_insert(layer_get_list(output, layer), &layer_surface->link);

        struct wlr_scene_tree *scene = layer_get_scene(layer);
        wlr_scene_node_reparent(&layer_surface->scene->tree->node, scene);
    }

    // if something has changed we rearange all the surfaces
    if(committed) {
        layer_surfaces_configure(output);
    }

    if(server.config->blur && layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
        wlr_scene_optimized_blur_mark_dirty(output->blur);
    }
}

static void
layer_surface_handle_map(struct wl_listener *listener, void *data) {
    struct layer_surface *layer_surface = wl_container_of(listener, layer_surface, map);

    // we reconfigure this outputs layer surfaces
    struct output *output = layer_surface->wlr_layer_surface->output->data;
    layer_surfaces_configure(output);

    // and give focus to this one (if it wants focus)
    focus_layer_surface(layer_surface);
}

bool
try_focus_exclusive_layer_surface(void) {
    struct output *iter_output;
    wl_list_for_each(iter_output, &server.outputs, link) {
        struct layer_surface *iter_layer_surface;
        wl_list_for_each(iter_layer_surface, &iter_output->layers.overlay, link) {
            if(iter_layer_surface->wlr_layer_surface->current.keyboard_interactive ==
                    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
                focus_layer_surface(iter_layer_surface);
                return true;
            }
        }
        wl_list_for_each(iter_layer_surface, &iter_output->layers.top, link) {
            if(iter_layer_surface->wlr_layer_surface->current.keyboard_interactive ==
                    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
                focus_layer_surface(iter_layer_surface);
                return true;
            }
        }
    }

    return false;
}

static void
layer_surface_handle_unmap(struct wl_listener *listener, void *data) {
    struct layer_surface *layer_surface = wl_container_of(listener, layer_surface, unmap);

    wl_list_remove(&layer_surface->link);
    struct output *output = layer_surface->wlr_layer_surface->output->data;

    // hack when the output has been destroyed, idk why is works, will have to inverstigate when i am back at setup todo
    if(output == NULL) {
        if(layer_surface == server.focused_layer_surface) {
            server.focused_layer_surface = NULL;
            server.exclusive = false;
        }
        wlr_layer_surface_v1_destroy(layer_surface->wlr_layer_surface);
        return;
    }

    if(layer_surface == server.focused_layer_surface) {
        // focusing next will set it if needed
        server.exclusive = false;

        if(!try_focus_exclusive_layer_surface()) {
            if(server.prev_focused != NULL && server.prev_focused->workspace == server.active_workspace) {
                focus_toplevel(server.prev_focused, false);
            } else if(has_floating(server.active_workspace)) {
                focus_toplevel(first_floating(server.active_workspace), false);
            } else if(has_masters(server.active_workspace)) {
                focus_toplevel(first_master(server.active_workspace), false);
            }
        }
    }

    if(server.config->blur && layer_surface->wlr_layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
        wlr_scene_optimized_blur_mark_dirty(output->blur);
    }

    layer_surfaces_configure(output);
}

static void
layer_surface_handle_destroy(struct wl_listener *listener, void *data) {
    struct layer_surface *layer_surface = wl_container_of(listener, layer_surface, destroy);

    wl_list_remove(&layer_surface->map.link);
    wl_list_remove(&layer_surface->unmap.link);
    wl_list_remove(&layer_surface->destroy.link);

    free(layer_surface);
}

static void
layer_surface_handle_new_popup(struct wl_listener *listener, void *data) {
    struct layer_surface *layer_surface = wl_container_of(listener, layer_surface, new_popup);
    struct wlr_xdg_popup *xdg_popup = data;

    // see server_handle_new_popup()
    struct popup *popup = xdg_popup->base->data;

    popup->scene_tree = wlr_scene_xdg_surface_create(layer_surface->scene->tree, xdg_popup->base);
    view_create_for_node(&popup->scene_tree->node, VIEW_POPUP, popup);
}

void
focus_layer_surface(struct layer_surface *layer_surface) {
    if(server.mode == SERVER_MODE_LOCKED)
        return;

    enum zwlr_layer_surface_v1_keyboard_interactivity keyboard_interactive =
            layer_surface->wlr_layer_surface->current.keyboard_interactive;

    if(keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE ||
            (keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND && server.exclusive))
        return;

    // unfocus the focused toplevel. note: even tho focused_layer_surface can also be set, we dont have anything special
    // to do to make it unfocused; invoking the keyboard enter function will stop it from getting the new events
    if(server.focused_toplevel != NULL) {
        // keep it in this field, so it can be returned focus later
        server.prev_focused = server.focused_toplevel;
        unfocus_focused_toplevel();
    }

    enum zwlr_layer_shell_v1_layer layer = layer_surface->wlr_layer_surface->current.layer;

    server.focused_layer_surface = layer_surface;
    // we set it to exclusive focus only if its in the top or overlay layer
    server.exclusive = layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP &&
            keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat);
    if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server.seat, layer_surface->wlr_layer_surface->surface, keyboard->keycodes,
                keyboard->num_keycodes, &keyboard->modifiers);
    }
}

static void
layer_surfaces_configure_layer(struct output *output, enum zwlr_layer_shell_v1_layer layer, bool exclusive) {
    struct wl_list *list = layer_get_list(output, layer);

    struct wlr_box full_area;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &full_area);

    struct layer_surface *l;
    wl_list_for_each_reverse(l, list, link) {
        if((l->wlr_layer_surface->current.exclusive_zone > 0) != exclusive)
            continue;

        wlr_scene_layer_surface_v1_configure(l->scene, &full_area, &output->usable_area);
    }
}

void
layer_surfaces_configure(struct output *output) {
    struct wlr_box full_area;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &full_area);

    output->usable_area = full_area;

    // first configure all the exclusive ones
    for(size_t i = 0; i < 4; i++) {
        layer_surfaces_configure_layer(output, i, true);
    }

    // then the others
    for(size_t i = 0; i < 4; i++) {
        layer_surfaces_configure_layer(output, i, false);
    }

    struct workspace *iter;
    wl_list_for_each(iter, &output->workspaces, link) {
        layout_configure(iter);
    }
}

// we dont touch background as we want it seen behind the fullscreened toplevel in order for blur to work
void
layers_under_fullscreen_set_enabled(struct output *output, bool enable) {
    struct layer_surface *l;
    wl_list_for_each(l, &output->layers.bottom, link) {
        wlr_scene_node_set_enabled(&l->scene->tree->node, enable);
    }
    wl_list_for_each(l, &output->layers.top, link) {
        wlr_scene_node_set_enabled(&l->scene->tree->node, enable);
    }
}

void
server_handle_new_layer_surface(struct wl_listener *listener, void *data) {
    struct wlr_layer_surface_v1 *wlr_layer_surface = data;

    struct layer_surface *layer_surface = calloc(1, sizeof(*layer_surface));
    layer_surface->wlr_layer_surface = wlr_layer_surface;
    wlr_layer_surface->data = layer_surface;

    if(layer_surface->wlr_layer_surface->output == NULL) {
        // we give it currently active output
        layer_surface->wlr_layer_surface->output = server.active_workspace->output->wlr_output;
    }

    struct output *output = layer_surface->wlr_layer_surface->output->data;
    wlr_fractional_scale_v1_notify_scale(layer_surface->wlr_layer_surface->surface, output->wlr_output->scale);
    wlr_surface_set_preferred_buffer_scale(layer_surface->wlr_layer_surface->surface, ceil(output->wlr_output->scale));

    // insert it into a list
    enum zwlr_layer_shell_v1_layer layer = wlr_layer_surface->pending.layer;
    wl_list_insert(layer_get_list(output, layer), &layer_surface->link);

    // and create a scene for it
    layer_surface->scene = wlr_scene_layer_surface_v1_create(layer_get_scene(layer), wlr_layer_surface);
    view_create_for_node(&layer_surface->scene->tree->node, VIEW_LAYER_SURFACE, layer_surface);

    if(output->active_workspace->fullscreen != NULL &&
            (layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM || layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP)) {
        wlr_scene_node_set_enabled(&layer_surface->scene->tree->node, false);
    }

    // check rules for bluring and stuff
    layer_surface_check_rules(layer_surface);

    layer_surface->commit.notify = layer_surface_handle_commit;
    wl_signal_add(&wlr_layer_surface->surface->events.commit, &layer_surface->commit);

    layer_surface->map.notify = layer_surface_handle_map;
    wl_signal_add(&wlr_layer_surface->surface->events.map, &layer_surface->map);

    layer_surface->unmap.notify = layer_surface_handle_unmap;
    wl_signal_add(&wlr_layer_surface->surface->events.unmap, &layer_surface->unmap);

    layer_surface->new_popup.notify = layer_surface_handle_new_popup;
    wl_signal_add(&wlr_layer_surface->events.new_popup, &layer_surface->new_popup);

    layer_surface->destroy.notify = layer_surface_handle_destroy;
    wl_signal_add(&wlr_layer_surface->surface->events.destroy, &layer_surface->destroy);
}
