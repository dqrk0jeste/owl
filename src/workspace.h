#pragma once

#include <scenefx/types/wlr_scene.h>

#include "config.h"
#include "toplevel.h"
#include "output.h"

#include <wayland-server-protocol.h>

struct mwc_animation;

struct mwc_workspace {
    struct wl_list link;

    struct mwc_output *output;
    uint32_t index;
    struct workspace_config *config;

    struct wl_list masters;
    struct wl_list slaves;
    struct wl_list floating_toplevels;
    struct mwc_toplevel *fullscreen_toplevel;
};

void
workspace_create_for_output(struct mwc_output *output, struct workspace_config *config);

void
change_workspace(struct mwc_workspace *workspace, bool keep_focus);

void
toplevel_move_to_workspace(struct mwc_toplevel *toplevel, struct mwc_workspace *workspace);

struct mwc_toplevel *
workspace_find_closest_floating_toplevel(struct mwc_workspace *workspace,
        enum mwc_direction side);
