#pragma once

#include <libinput.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>

struct pointer {
    struct wlr_pointer *wlr_pointer;
    struct wl_list link;

    struct wl_listener destroy;
};

void
pointer_configure(struct pointer *pointer);

void
handle_new_pointer(struct wlr_input_device *device);
