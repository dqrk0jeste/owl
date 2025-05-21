#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_session_lock_v1.h>

#include "view.h"

struct lock {
    struct wlr_session_lock_v1 *wlr_lock;
    bool locked;

    struct wl_list surfaces;

    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
};

struct lock_surface {
    struct wlr_session_lock_surface_v1 *wlr_lock_surface;
    struct wlr_scene_tree *scene_tree;
    struct lock *lock;

    struct wl_list link;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
};

struct lock_manager {
    struct wlr_session_lock_manager_v1 *base;
    struct lock *current_lock;

    struct wl_listener destroy;
    struct wl_listener new_lock;
};

void
focus_lock_surface(struct lock_surface *lock_surface);

void
lock_manager_init(void);
