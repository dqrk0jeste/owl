#pragma once

#include <wlr/types/wlr_xdg_shell.h>

struct owl_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener commit;
	struct wl_listener destroy;
};

