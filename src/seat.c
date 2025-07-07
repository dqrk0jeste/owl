#include "seat.h"

#include <assert.h>
#include <stdint.h>
#include <sys/types.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "keyboard.h"
#include "mwc.h"
#include "pointer.h"

extern struct server server;

// large portion of this was taken from labwc; huge thanks to consolatis
static void
handle_request_drag(struct wl_listener *listener, void *data) {
    if(server.mode > SERVER_MODE_NORMAL)
        return;

    server.mode = SERVER_MODE_DRAGGING;
    struct wlr_seat_request_start_drag_event *event = data;
    wlr_seat_start_pointer_drag(server.seat.base, event->drag, event->serial);
}

static void
handle_request_start_drag(struct wl_listener *listener, void *data) {
    struct wlr_drag *drag = data;

    if(drag->icon != NULL) {
        wlr_scene_drag_icon_create(server.drag_icon_tree, drag->icon);
        wlr_scene_node_raise_to_top(&server.drag_icon_tree->node);
        wlr_scene_node_set_enabled(&server.drag_icon_tree->node, true);
    }

    wl_signal_add(&drag->events.destroy, &server.seat.request_destroy_drag);
}

static void
handle_destroy_drag(struct wl_listener *listener, void *data) {
    server.mode = SERVER_MODE_NORMAL;

    wl_list_remove(&server.seat.request_destroy_drag.link);
    wlr_scene_node_set_enabled(&server.drag_icon_tree->node, false);
}

static void
handle_new_input(struct wl_listener *listener, void *data) {
    struct wlr_input_device *input = data;

    switch(input->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            handle_new_keyboard(input);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            handle_new_pointer(input);
            break;
        default:
            // mwc doesnt support touch devices, drawing tablets etc
            break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if(!wl_list_empty(&server.keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }

    wlr_seat_set_capabilities(server.seat.base, caps);
}

static void
handle_request_set_selection(struct wl_listener *listener, void *data) {
    // this event is raised by the seat when a client wants to set the selection, usually when the user copies something
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server.seat.base, event->source, event->serial);
}

void
seat_init(void) {
    // configures a seat, which is a single seat at which a user sits and operates the computer. this conceptually
    // includes up to one keyboard, pointer, touch, and drawing tablet device.
    server.seat.base = wlr_seat_create(server.display, "seat0");

    // inputs
    wl_list_init(&server.keyboards);
    wl_list_init(&server.pointers);

    server.seat.new_input.notify = handle_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.seat.new_input);

    server.seat.request_set_selection.notify = handle_request_set_selection;
    wl_signal_add(&server.seat.base->events.request_set_selection, &server.seat.request_set_selection);

    // configure drag presentation and listeners
    wlr_scene_node_set_enabled(&server.drag_icon_tree->node, false);

    server.seat.request_drag.notify = handle_request_drag;
    wl_signal_add(&server.seat.base->events.request_start_drag, &server.seat.request_drag);

    server.seat.request_start_drag.notify = handle_request_start_drag;
    wl_signal_add(&server.seat.base->events.start_drag, &server.seat.request_start_drag);
    server.seat.request_destroy_drag.notify = handle_destroy_drag;
    // we dont wire this one up since it is wired per drag
}

void
dnd_icons_move(int x, int y) {
    wlr_scene_node_set_position(&server.drag_icon_tree->node, x, y);
}
