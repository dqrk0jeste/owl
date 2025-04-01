#pragma once

#include "something.h"
#include <wayland-server-core.h>
#include <wlr/types/wlr_session_lock_v1.h>

struct mwc_lock {
    struct wlr_session_lock_v1 *wlr_lock;
    bool locked;

    struct wl_list surfaces;

    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
};

struct mwc_lock_surface {
    struct wlr_session_lock_surface_v1 *wlr_lock_surface;
    struct wlr_scene_tree *scene_tree;
    struct mwc_something something;
    struct mwc_lock *lock;

    struct wl_list link;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
};

void
session_lock_manager_handle_new(struct wl_listener *listener, void *data);

void
session_lock_manager_handle_destroy(struct wl_listener *listener, void *data);

void
focus_lock_surface(struct mwc_lock_surface *lock_surface);
