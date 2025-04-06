#pragma once

#include <scenefx/types/wlr_scene.h>

#include "view.h"

#include <wlr/types/wlr_xdg_shell.h>

struct mwc_popup {
    struct wlr_xdg_popup *xdg_popup;
    struct wlr_scene_tree *scene_tree;

    struct wl_listener commit;
    struct wl_listener destroy;
};

void
server_handle_new_popup(struct wl_listener *listener, void *data);

struct mwc_view *
popup_get_root_parent(struct mwc_popup *popup);
