#pragma once

#include <libinput.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>

struct mwc_pointer {
    struct wlr_pointer *wlr_pointer;
    const char *name;
    struct wl_list link;

    struct wl_listener destroy;
};

struct mwc_pointer_constraint {
    struct wlr_pointer_constraint_v1 *wlr_pointer_constraint;

    struct wl_listener destroy;
};

enum mwc_cursor_mode {
    MWC_CURSOR_PASSTHROUGH,
    MWC_CURSOR_MOVE,
    MWC_CURSOR_RESIZE,
};

void
server_handle_new_pointer(struct wlr_input_device *device);

bool
pointer_configure(struct mwc_pointer *pointer);

void
cursor_handle_motion(uint32_t time);

void
cursor_stop_move_resize(void);

struct mwc_view *
pointer_get_view_under_cursor(void);

void
pointer_handle_focus(uint32_t time, bool handle_keyboard_focus);

void
server_handle_cursor_motion(struct wl_listener *listener, void *data);

void
server_handle_cursor_motion_absolute(struct wl_listener *listener, void *data);

void
server_handle_cursor_button(struct wl_listener *listener, void *data);

void
server_handle_cursor_axis(struct wl_listener *listener, void *data);

void
server_handle_cursor_frame(struct wl_listener *listener, void *data);

void
server_handle_new_constraint(struct wl_listener *listener, void *data);

void
server_handle_new_relative_pointer(struct wl_listener *listener, void *data);

void
server_handle_relative_pointer_manager_destroy(struct wl_listener *listener, void *data);

