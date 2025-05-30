#include "cursor_shape.h"

#include "mwc.h"

extern struct server server;

void
cursor_shape_manager_handle_destroy(struct wl_listener *listener, void *data) {
    wl_list_remove(&server.cursor_shape_manager.set_cursor_shape.link);
    wl_list_remove(&server.cursor_shape_manager.destroy.link);
}

void
cursor_shape_manager_handle_request(struct wl_listener *listener, void *data) {
    if(server.cursor.is_hidden)
        return;

    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

    struct wlr_seat_client *focused_client = server.seat.base->pointer_state.focused_client;
    if(focused_client == event->seat_client) {
        cursor_set_image(wlr_cursor_shape_v1_name(event->shape));
    }
}

void
cursor_shape_manager_init(void) {
    server.cursor_shape_manager.base = wlr_cursor_shape_manager_v1_create(server.display, 1);

    server.cursor_shape_manager.set_cursor_shape.notify = cursor_shape_manager_handle_request;
    wl_signal_add(&server.cursor_shape_manager.base->events.request_set_shape,
            &server.cursor_shape_manager.set_cursor_shape);

    server.cursor_shape_manager.destroy.notify = cursor_shape_manager_handle_destroy;
    wl_signal_add(&server.cursor_shape_manager.base->events.destroy, &server.cursor_shape_manager.destroy);
}
