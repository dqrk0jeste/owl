#pragma once

#include <scenefx/types/wlr_scene.h>
#include <wayland-server-protocol.h>

#include "output.h"
#include "toplevel.h"

struct workspace {
    struct wl_list link;

    struct output *output;
    uint32_t index;

    // when this workspace is reparented by another output, we keep the name of the original output that created this
    // workspace so we can return it back later if that output is reenabled
    char *original_output;

    double master_ratio;

    struct wl_list masters;
    struct wl_list slaves;
    struct wl_list floating;
    struct toplevel *fullscreen;
};

bool
has_floating(struct workspace *workspace);

struct toplevel *
next_floating(struct toplevel *toplevel);

struct toplevel *
prev_floating(struct toplevel *toplevel);

struct toplevel *
first_floating(struct workspace *workspace);

struct toplevel *
last_floating(struct workspace *workspace);

void
change_workspace(struct workspace *workspace, bool keep_focus);

void
toplevel_move_to_workspace(struct toplevel *toplevel, struct workspace *workspace);

struct toplevel *
workspace_find_closest_floating_toplevel(struct workspace *workspace, enum direction side);

// note: this does not include the fullscreen toplevel if there is one
void
workspace_toplevels_set_enabled(struct workspace *workspace, bool enabled);

void
workspace_set_master_ratio(struct workspace *workspace, double master_ratio);
