#include "cursor.h"

#include <wlr/types/wlr_cursor.h>

#include "mwc.h"

extern struct server server;

void
cursor_shape_manager_handle_destroy(struct wl_listener *listener, void *data) {
    wl_list_remove(&server.request_cursor_shape.link);
    wl_list_remove(&server.cursor_shape_manager_destroy.link);
}

void
cursor_shape_manager_handle_request(struct wl_listener *listener, void *data) {
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

    struct wlr_seat_client *focused_client = server.seat->pointer_state.focused_client;
    if(focused_client == event->seat_client) {
        const char *name = wlr_cursor_shape_v1_name(event->shape);
        wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, name);
    }
}

void
cursor_handle_request(struct wl_listener *listener, void *data) {
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    struct wlr_seat_client *focused_client = server.seat->pointer_state.focused_client;
    if(focused_client == event->seat_client) {
        wlr_cursor_set_surface(server.cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

void
cursor_set_xcursor_variables(char *theme, uint32_t size) {
    char cursor_size[8];
    snprintf(cursor_size, sizeof(cursor_size), "%u", size);
    setenv("XCURSOR_SIZE", cursor_size, true);

    if(theme != NULL) {
        setenv("XCURSOR_THEME", theme, true);
    } else {
        setenv("XCURSOR_THEME", "", true);
    }
}
