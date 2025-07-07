#include "foreign_toplevel.h"

#include <stdlib.h>

#include "mwc.h"
#include "toplevel.h"

extern struct server server;

static void
handle_request_activate(struct wl_listener *listener, void *data) {
    struct foreign_toplevel_handle *handle = wl_container_of(listener, handle, request_activate);

    focus_toplevel(handle->toplevel, false);
}

static void
handle_request_fullscreen(struct wl_listener *listener, void *data) {
    struct foreign_toplevel_handle *handle = wl_container_of(listener, handle, request_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;

    if(event->fullscreen) {
        toplevel_set_fullscreen(handle->toplevel);
    } else {
        toplevel_unset_fullscreen(handle->toplevel);
    }
}

static void
handle_set_rectangle(struct wl_listener *listener, void *data) {
    struct foreign_toplevel_handle *handle = wl_container_of(listener, handle, set_rectangle);
    struct wlr_foreign_toplevel_handle_v1_set_rectangle_event *event = data;

    if(handle->toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        toplevel_set_state(handle->toplevel, (struct wlr_box){event->x, event->y, event->width, event->height});
    }
}

static void
handle_request_close(struct wl_listener *listener, void *data) {
    struct foreign_toplevel_handle *handle = wl_container_of(listener, handle, request_close);

    wlr_xdg_toplevel_send_close(handle->toplevel->xdg_toplevel);
}

struct foreign_toplevel_handle *
foreign_toplevel_handle_create(struct toplevel *toplevel) {
    struct foreign_toplevel_handle *handle = calloc(1, sizeof(*handle));
    handle->wlr_handle = wlr_foreign_toplevel_handle_v1_create(server.foreign_toplevel_manager);
    handle->toplevel = toplevel;

    handle->request_activate.notify = handle_request_activate;
    wl_signal_add(&handle->wlr_handle->events.request_activate, &handle->request_activate);

    handle->request_fullscreen.notify = handle_request_fullscreen;
    wl_signal_add(&handle->wlr_handle->events.request_fullscreen, &handle->request_fullscreen);

    handle->set_rectangle.notify = handle_set_rectangle;
    wl_signal_add(&handle->wlr_handle->events.set_rectangle, &handle->set_rectangle);

    handle->request_close.notify = handle_request_close;
    wl_signal_add(&handle->wlr_handle->events.request_close, &handle->request_close);

    return handle;
}

void
foreign_toplevel_handle_destroy(struct foreign_toplevel_handle *handle) {
    wlr_foreign_toplevel_handle_v1_destroy(handle->wlr_handle);

    wl_list_remove(&handle->request_activate.link);
    wl_list_remove(&handle->request_fullscreen.link);
    wl_list_remove(&handle->set_rectangle.link);
    wl_list_remove(&handle->request_close.link);

    free(handle);
}
