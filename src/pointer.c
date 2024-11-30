
void
server_handle_new_pointer(struct wlr_input_device *device) {
  /* enable natural scrolling and tap to click*/
  if(wlr_input_device_is_libinput(device)) {
    struct libinput_device *libinput_device = wlr_libinput_get_device_handle(device);

    if(libinput_device_config_scroll_has_natural_scroll(libinput_device)
      && server.config->natural_scroll) {
      libinput_device_config_scroll_set_natural_scroll_enabled(libinput_device, true);
    }
    if(libinput_device_config_tap_get_finger_count(libinput_device)
      && server.config->tap_to_click) {
      libinput_device_config_tap_set_enabled(libinput_device, true);
    }
  }

  wlr_cursor_attach_input_device(server.cursor, device);
}

static void
server_reset_cursor_mode() {
  /* reset the cursor mode to passthrough. */
  server.cursor_mode = OWL_CURSOR_PASSTHROUGH;
  server.grabbed_toplevel->resizing = false;
  server.grabbed_toplevel = NULL;

  if(server.client_cursor.surface != NULL) {
    wlr_cursor_set_surface(server.cursor, server.client_cursor.surface,
                           server.client_cursor.hotspot_x, server.client_cursor.hotspot_y);
  } else {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
  }
}

static void
cursor_handle_motion(uint32_t time) {
  /* get the output that the cursor is on currently */
  struct wlr_output *wlr_output = wlr_output_layout_output_at(
    server.output_layout, server.cursor->x, server.cursor->y);
  struct owl_output *output = wlr_output->data;

  /* set global active workspace */
  if(output->active_workspace != server.active_workspace) {
    server.active_workspace = output->active_workspace;
    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);
  }

  if(server.cursor_mode == OWL_CURSOR_MOVE) {
    toplevel_move();
    return;
  } else if (server.cursor_mode == OWL_CURSOR_RESIZE) {
    toplevel_resize();
    return;
  }

  /* find something under the pointer and send the event along. */
  double sx, sy;
  struct wlr_seat *seat = server.seat;
  struct wlr_surface *surface = NULL;
  struct owl_something *something =
    something_at(server.cursor->x, server.cursor->y, &surface, &sx, &sy);

  if(something == NULL) {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
    /* clear pointer focus so future button events and such are not sent to
     * the last client to have the cursor over it */
    wlr_seat_pointer_clear_focus(seat);
    return;
  }

  if(something->type == OWL_TOPLEVEL) {
    focus_toplevel(something->toplevel);
  } else {
    focus_layer_surface(something->layer_surface);
  }

  wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
  wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

static void
server_handle_cursor_motion(struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_event *event = data;
  wlr_cursor_move(server.cursor, &event->pointer->base,
                  event->delta_x, event->delta_y);
  cursor_handle_motion(event->time_msec);
}


static void
server_handle_cursor_motion_absolute(
  struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_absolute_event *event = data;
  wlr_cursor_warp_absolute(server.cursor, &event->pointer->base, event->x, event->y);
  process_cursor_motion(event->time_msec);
}

/* TODO: add mouse button shortcuts */
static void
server_handle_cursor_button(struct wl_listener *listener, void *data) {
  struct wlr_pointer_button_event *event = data;

  /* notify the client with pointer focus that a button press has occurred */
  wlr_seat_pointer_notify_button(server.seat, event->time_msec,
                                 event->button, event->state);

  if(event->state == WL_POINTER_BUTTON_STATE_RELEASED
    && server.cursor_mode != OWL_CURSOR_PASSTHROUGH) {
    struct owl_output *primary_output = 
      toplevel_get_primary_output(server.grabbed_toplevel);

    if(primary_output != server.grabbed_toplevel->workspace->output) {
      server.grabbed_toplevel->workspace = primary_output->active_workspace;
      wl_list_remove(&server.grabbed_toplevel->link);
      wl_list_insert(&primary_output->active_workspace->floating_toplevels,
                     &server.grabbed_toplevel->link);
    }

    server_reset_cursor_mode();
  }
}


static void
server_handle_cursor_axis(struct wl_listener *listener, void *data) {
  struct wlr_pointer_axis_event *event = data;

  /* notify the client with pointer focus of the axis event */
  wlr_seat_pointer_notify_axis(server.seat,
                               event->time_msec, event->orientation, event->delta,
                               event->delta_discrete, event->source, event->relative_direction);
}

static void
server_handle_cursor_frame(struct wl_listener *listener, void *data) {
  wlr_seat_pointer_notify_frame(server.seat);
}

