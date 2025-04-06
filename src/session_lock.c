#include "session_lock.h"

#include "layer_surface.h"
#include "view.h"
#include "toplevel.h"
#include "mwc.h"
#include "rendering.h"

#include <wlr/util/log.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

extern struct mwc_server server;

void
lock_surface_handle_map(struct wl_listener *listener, void *data) {
    struct mwc_lock_surface *lock_surface = wl_container_of(listener, lock_surface, map);

    focus_lock_surface(lock_surface);
}

void
lock_surface_handle_unmap(struct wl_listener *listener, void *data) {
    struct mwc_lock_surface *lock_surface = wl_container_of(listener, lock_surface, unmap);

    wl_list_remove(&lock_surface->link);
    // we pass focus only if the thing is still locked
    if(lock_surface->lock->locked && !wl_list_empty(&lock_surface->lock->surfaces)) {
        struct mwc_lock_surface *next = wl_container_of(lock_surface->lock->surfaces.next, next, link);
        focus_lock_surface(next);
    }
}

void
lock_surface_handle_destroy(struct wl_listener *listener, void *data) {
    struct mwc_lock_surface *lock_surface = wl_container_of(listener, lock_surface, destroy);

    free(lock_surface);
}

void
session_lock_handle_new_surface(struct wl_listener *listener, void *data) {
    struct mwc_lock *lock = wl_container_of(listener, lock, new_surface);

    struct wlr_session_lock_surface_v1 *wlr_lock_surface = data;
    struct mwc_output *output = wlr_lock_surface->output->data;

    struct mwc_lock_surface *lock_surface = calloc(1, sizeof(*lock_surface));
    lock_surface->wlr_lock_surface = wlr_lock_surface;

    wl_list_insert(&lock->surfaces, &lock_surface->link);

    wlr_lock_surface->data = lock_surface;
    lock_surface->lock = lock;

    lock_surface->scene_tree = wlr_scene_subsurface_tree_create(server.session_lock_tree,
                                                                wlr_lock_surface->surface);
    view_create_for_node(&lock_surface->scene_tree->node, MWC_LOCK_SURFACE, lock_surface);

    lock_surface->map.notify = lock_surface_handle_map;
    wl_signal_add(&wlr_lock_surface->surface->events.map, &lock_surface->map);

    lock_surface->unmap.notify = lock_surface_handle_unmap;
    wl_signal_add(&wlr_lock_surface->surface->events.unmap, &lock_surface->unmap);

    lock_surface->destroy.notify = lock_surface_handle_destroy;
    wl_signal_add(&wlr_lock_surface->surface->events.destroy, &lock_surface->destroy);

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    wlr_scene_node_set_position(&lock_surface->scene_tree->node, output_box.x, output_box.y);
    wlr_session_lock_surface_v1_configure(wlr_lock_surface, output_box.width, output_box.height);
}

void
focus_lock_surface(struct mwc_lock_surface *lock_surface) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat);
    if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server.seat, lock_surface->wlr_lock_surface->surface,
                                       keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
}

void
session_lock_handle_unlock(struct wl_listener *listener, void *data) {
    struct mwc_lock *lock = wl_container_of(listener, lock, unlock);
    lock->locked = false;
    server.lock = NULL;

    struct wlr_output *wlr_output = wlr_output_layout_output_at(server.output_layout,
                                                                server.cursor->x, server.cursor->y);
    struct mwc_output *output = wlr_output->data;

    // optimize this
    bool focused = false;
    struct mwc_layer_surface *l;
    wl_list_for_each(l, &output->layers.overlay, link) {
        if(l->wlr_layer_surface->current.keyboard_interactive) {
            focus_layer_surface(l);
            focused = true;
        }
    }
    wl_list_for_each(l, &output->layers.top, link) {
        if(l->wlr_layer_surface->current.keyboard_interactive) {
            focus_layer_surface(l);
            focused = true;
        }
    }

    if(!focused) {
        if(!wl_list_empty(&server.active_workspace->masters)) {
            struct mwc_toplevel *first = wl_container_of(server.active_workspace->masters.next,
                                                         first, link);
            focus_toplevel(first);
        } else if(!wl_list_empty(&server.active_workspace->floating_toplevels)) {
            struct mwc_toplevel *first = wl_container_of(server.active_workspace->floating_toplevels.next,
                                                         first, link);
            focus_toplevel(first);
        }
    }

    struct mwc_output *o;
    wl_list_for_each(o, &server.outputs, link) {
        // destroy the rectangle blocking the view
        wlr_scene_node_destroy(&o->session_lock_rect->node);
        o->session_lock_rect = NULL;
    }
}

void
session_lock_handle_destroy(struct wl_listener *listener, void *data) {
    struct mwc_lock *lock = wl_container_of(listener, lock, destroy);

    if(lock->locked) {
        wlr_log(WLR_ERROR, "lock surface destroyed without being unlocked");
    }

    wl_list_remove(&lock->destroy.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->new_surface.link);

    free(lock);
}

void
session_lock_manager_handle_destroy(struct wl_listener *listener, void *data) {
    wl_list_remove(&server.lock_manager_destroy.link);
    wl_list_remove(&server.new_lock.link);
}

void
session_lock_manager_handle_new(struct wl_listener *listener, void *data) {
    struct wlr_session_lock_v1 *wlr_lock = data;

    if(server.lock != NULL) {
        wlr_log(WLR_ERROR, "session already locked");
        wlr_session_lock_v1_destroy(wlr_lock);
        return;
    }

    struct mwc_lock *lock = calloc(1, sizeof(*lock));
    lock->wlr_lock = wlr_lock;
    lock->locked = true;

    wl_list_init(&lock->surfaces);

    server.lock = lock;

    float black[4] = { 0.0, 0.0, 0.0, 1.0 };
    struct mwc_output *o;
    wl_list_for_each(o, &server.outputs, link) {
        struct wlr_box output_box;
        wlr_output_layout_get_box(server.output_layout, o->wlr_output, &output_box);

        o->session_lock_rect = wlr_scene_rect_create(server.session_lock_tree,
                                                     output_box.width, output_box.height, black);
        wlr_scene_node_set_position(&o->session_lock_rect->node, output_box.x, output_box.y);
    }

    // todo: needs improvement
    unfocus_focused_toplevel();

    lock->new_surface.notify = session_lock_handle_new_surface;
    wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);

    lock->unlock.notify = session_lock_handle_unlock;
    wl_signal_add(&wlr_lock->events.unlock, &lock->unlock);

    lock->destroy.notify = session_lock_handle_destroy;
    wl_signal_add(&wlr_lock->events.destroy, &lock->destroy);

    wlr_session_lock_v1_send_locked(wlr_lock);
}

