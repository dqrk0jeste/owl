#include "dnd.h"

#include "owl.h"

#include <assert.h>
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

/* large portion of this was taken from labwc; huge thanks to consolatis */

extern struct owl_server server;

void
server_handle_request_drag(struct wl_listener *listener, void *data) {
	struct wlr_seat_request_start_drag_event *event = data;
  wlr_seat_start_pointer_drag(server.seat, event->drag, event->serial);
}

void
server_handle_request_start_drag(struct wl_listener *listener, void *data) {
  server.drag_active = true;
	struct wlr_drag *drag = data;

	if(drag->icon != NULL) {
		wlr_scene_drag_icon_create(server.drag_icon_tree, drag->icon);
		wlr_scene_node_raise_to_top(&server.drag_icon_tree->node);
		wlr_scene_node_set_enabled(&server.drag_icon_tree->node, true);
	}

	wl_signal_add(&drag->events.destroy, &server.request_destroy_drag);
}

void
server_handle_destroy_drag(struct wl_listener *listener, void *data) {
  server.drag_active = false;
	wl_list_remove(&server.request_destroy_drag.link);
	wlr_scene_node_set_enabled(&server.drag_icon_tree->node, false);
}

void
dnd_icons_move(uint32_t x, uint32_t y) {
	wlr_scene_node_set_position(&server.drag_icon_tree->node, x, y);
}
