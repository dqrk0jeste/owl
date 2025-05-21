#include "relative_pointer.h"

#include "mwc.h"

extern struct server server;

static void
handle_destroy(struct wl_listener *listener, void *data) {
    wl_list_remove(&server.relative_pointer_manager.destroy.link);
}

void
relative_pointer_manager_init(void) {
    server.relative_pointer_manager.base = wlr_relative_pointer_manager_v1_create(server.display);

    server.relative_pointer_manager.destroy.notify = handle_destroy;
    wl_signal_add(&server.relative_pointer_manager.base->events.destroy, &server.relative_pointer_manager.destroy);
}
