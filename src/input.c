#include "input.h"

#include <wlr/types/wlr_data_device.h>

#include "keyboard.h"
#include "mwc.h"
#include "pointer.h"

extern struct server server;

void
server_handle_new_input(struct wl_listener *listener, void *data) {
    struct wlr_input_device *input = data;

    switch(input->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_handle_new_keyboard(input);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            server_handle_new_pointer(input);
            break;
        default:
            // mwc doesnt support touch devices, drawing tablets etc
            break;
    }

    // we need to let the wlr_seat know what our capabilities are, which is
    // communiciated to the client. we always have a cursor, even if
    // there are no pointer devices, so we always include that capability
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if(!wl_list_empty(&server.keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }

    wlr_seat_set_capabilities(server.seat, caps);
}

void
server_handle_request_set_selection(struct wl_listener *listener, void *data) {
    // this event is raised by the seat when a client wants to set the selection, usually when the user copies something
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server.seat, event->source, event->serial);
}
