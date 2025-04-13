#pragma once

#include <scenefx/types/wlr_scene.h>
#include <wayland-server-protocol.h>

#include "output.h"
#include "toplevel.h"

struct mwc_animation;

struct mwc_workspace {
    struct wl_list link;

    struct mwc_output *output;
    uint32_t index;

    // when this workspace is reparented by another output, we keep the name
    // of the original output that created this workspace so we can return it back later
    // if that output is reenabled
    char *original_output;

    struct wl_list masters;
    struct wl_list slaves;
    struct wl_list floating_toplevels;
    struct mwc_toplevel *fullscreen_toplevel;
};

void
change_workspace(struct mwc_workspace *workspace, bool keep_focus);

void
toplevel_move_to_workspace(struct mwc_toplevel *toplevel, struct mwc_workspace *workspace);

struct mwc_toplevel *
workspace_find_closest_floating_toplevel(struct mwc_workspace *workspace, enum mwc_direction side);

void
workspace_toplevels_set_enabled(struct mwc_workspace *workspace, bool enabled);
