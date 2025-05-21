#pragma once

#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "view.h"

struct popup {
    struct wlr_xdg_popup *xdg_popup;
    struct wlr_scene_tree *scene_tree;

    struct wl_listener commit;
    struct wl_listener destroy;
};

void
handle_new_popup(struct wl_listener *listener, void *data);

struct view *
popup_get_root_parent(struct popup *popup);
