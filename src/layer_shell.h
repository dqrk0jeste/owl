#pragma once

#include <stdbool.h>
#include <wlr/types/wlr_layer_shell_v1.h>

#include "decoration.h"
#include "view.h"

struct layer_surface {
    struct wl_list link;
    struct wlr_layer_surface_v1 *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1 *scene;

    enum blur blur;
    bool blur_ignore_transparent;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener new_popup;
    struct wl_listener destroy;
};

struct layer_shell {
    struct wlr_layer_shell_v1 *base;

    struct wl_listener new_layer_surface;
};

struct output;

void
layer_surfaces_configure(struct output *output);

void
focus_layer_surface(struct layer_surface *layer_surface);

void
layers_under_fullscreen_set_enabled(struct output *output, bool enable);

bool
try_focus_exclusive_layer_surface(void);

void
layer_shell_init(void);
