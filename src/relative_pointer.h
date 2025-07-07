#pragma once

#include <wlr/types/wlr_relative_pointer_v1.h>

struct relative_pointer_manager {
    struct wlr_relative_pointer_manager_v1 *base;

    struct wl_listener destroy;
};

void
relative_pointer_manager_init(void);
