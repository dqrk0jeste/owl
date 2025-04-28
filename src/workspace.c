#include "workspace.h"

#include <assert.h>
#include <scenefx/types/wlr_scene.h>
#include <stdint.h>

#include "helpers.h"
#include "ipc.h"
#include "layer_surface.h"
#include "layout.h"
#include "mwc.h"
#include "view.h"

extern struct server server;

bool
has_floating(struct workspace *workspace) {
    return !wl_list_empty(&workspace->floating);
}

struct toplevel *
next_floating(struct toplevel *toplevel) {
    if(toplevel->link.next == &toplevel->workspace->floating)
        return NULL;

    struct toplevel *t = wl_container_of(toplevel->link.next, t, link);
    return t;
}

struct toplevel *
prev_floating(struct toplevel *toplevel) {
    if(toplevel->link.prev == &toplevel->workspace->floating)
        return NULL;

    struct toplevel *t = wl_container_of(toplevel->link.prev, t, link);
    return t;
}

struct toplevel *
first_floating(struct workspace *workspace) {
    if(wl_list_empty(&workspace->floating))
        return NULL;

    struct toplevel *t = wl_container_of(workspace->floating.next, t, link);
    return t;
}

struct toplevel *
last_floating(struct workspace *workspace) {
    if(wl_list_empty(&workspace->floating))
        return NULL;

    struct toplevel *t = wl_container_of(workspace->floating.prev, t, link);
    return t;
}

static void
handle_focus(struct workspace *workspace) {
    if(workspace->fullscreen != NULL) {
        focus_toplevel(workspace->fullscreen, false);
    } else if(has_floating(workspace)) {
        focus_toplevel(first_floating(workspace), false);
    } else if(has_masters(workspace)) {
        focus_toplevel(first_master(workspace), false);
    } else {
        unfocus_focused_toplevel();
    }
}

void
change_workspace(struct workspace *workspace, bool keep_focus) {
    // if it is the same as global active workspace, do nothing
    if(server.active_workspace == workspace)
        return;

    // if this workspace is not presented on its output we handle the presentation first
    if(workspace != workspace->output->active_workspace) {
        // disable all the toplevels on the current workspace on the output
        struct workspace *current_workspace = workspace->output->active_workspace;
        workspace_toplevels_set_enabled(current_workspace, false);
        if(current_workspace->fullscreen != NULL) {
            wlr_scene_node_set_enabled(&current_workspace->fullscreen->scene_tree->node, false);
        }

        if(workspace->fullscreen != NULL) {
            // if there is a fullscreen toplevel we only enable that one
            wlr_scene_node_set_enabled(&workspace->fullscreen->scene_tree->node, true);
            layers_under_fullscreen_set_enabled(workspace->output, false);
        } else {
            // else enable all of the toplevels and layers (they may have been disabled if `current_workspace` had a
            // fullscreen toplevel on it)
            workspace_toplevels_set_enabled(workspace, true);
            layers_under_fullscreen_set_enabled(workspace->output, true);
        }
    }

    // warp the cursor if this output is not on the same output as currently globally active workspace
    if(server.active_workspace->output != workspace->output) {
        jump_cursor_to_output(workspace->output);
    }

    // set it as globally active workspace
    server.active_workspace = workspace;
    // and also as this outputs active workspace
    workspace->output->active_workspace = workspace;

    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);

    // handle the keyboard focus
    if(!keep_focus && server.mode <= SERVER_MODE_CAN_GIVE_FOCUS && !server.exclusive) {
        handle_focus(workspace);
    }

    // and pointer focus
    pointer_handle_focus(get_now_in_ms(), false);
}

static void
patch_relative_box_for_output(struct wlr_box *box, struct output *old, struct output *new) {
    // calculate where the toplevel should be placed after exiting fullscreen; we use the same relative
    // place on this output as is was on the last one
    int32_t old_relative_x = box->x - old->usable_area.x;
    double relative_x = (double)old_relative_x / old->usable_area.width;

    int32_t old_relative_y = box->y - old->usable_area.y;
    double relative_y = (double)old_relative_y / old->usable_area.height;

    box->x = new->usable_area.x + relative_x *new->usable_area.width;
    box->y = new->usable_area.y + relative_y *new->usable_area.height;
}

void
toplevel_move_to_workspace(struct toplevel *toplevel, struct workspace *workspace) {
    if(toplevel == server.grabbed_toplevel || toplevel->workspace == workspace || workspace->fullscreen != NULL)
        return;

    struct workspace *old_workspace = toplevel->workspace;
    toplevel->workspace = workspace;

    // handle server state
    if(toplevel->mode == TOPLEVEL_MODE_FULLSCREEN) {
        old_workspace->fullscreen = NULL;
        workspace->fullscreen = toplevel;
    } else if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        wl_list_remove(&toplevel->link);
        wl_list_insert(&workspace->floating, &toplevel->link);
    } else {
        wl_list_remove(&toplevel->link);

        // if its master we try to find its replacement
        if(toplevel->mode == TOPLEVEL_MODE_MASTER && has_slaves(old_workspace)) {
            promote_last_slave(old_workspace);
        }

        layout_add(workspace, toplevel);
    }

    // change the active workspace before handling the presentation, so the right damage is tracked and for some
    // animation optimizations while also keeping the focus on this toplevel
    change_workspace(workspace, true);

    // handle presentation
    if(toplevel->mode == TOPLEVEL_MODE_FULLSCREEN) {
        struct wlr_box output_box;
        wlr_output_layout_get_box(server.output_layout, workspace->output->wlr_output, &output_box);
        toplevel_set_state(toplevel, output_box);

        // if the output changed then we enable the layers on the old output
        if(old_workspace->output != workspace->output) {
            layers_under_fullscreen_set_enabled(old_workspace->output, true);
        }

        if(toplevel->prev_mode == TOPLEVEL_MODE_FLOATING && old_workspace->output != workspace->output) {
            // calculate where the toplevel should be placed after exiting fullscreen
            // we use the same relative place on this output as is was on the last one
            patch_relative_box_for_output(&toplevel->prev_deco_box, old_workspace->output, workspace->output);
        } else {
            // invalidate the index
            toplevel->prev_index = -1U;
            layout_configure(old_workspace);
        }
    } else if(toplevel->mode == TOPLEVEL_MODE_FLOATING && old_workspace->output != workspace->output) {
        // if the toplevel is moved between workspaces on the same output we dont do anything about the presentation;
        // else we place it at the same relative coords on the new output
        struct wlr_box box = toplevel->deco_box;
        patch_relative_box_for_output(&box, old_workspace->output, workspace->output);
        toplevel_set_state(toplevel, box);
    } else if(toplevel->mode == TOPLEVEL_MODE_MASTER || toplevel->mode == TOPLEVEL_MODE_SLAVE) {
        // and if tiled we just configure the layouts of both the old one and the new one
        layout_configure(old_workspace);
        layout_configure(workspace);
    }
}

struct toplevel *
workspace_find_closest_floating(struct workspace *workspace, enum direction side) {
    if(!has_floating(workspace))
        return NULL;

    struct toplevel *first = first_floating(workspace);
    struct toplevel *min_x = first, *max_x = first, *min_y = first, *max_y = first;

    struct toplevel *iter;
    wl_list_for_each(iter, &workspace->floating, link) {
        if(iter->deco_box.x < min_x->deco_box.x) {
            min_x = iter;
        } else if(iter->deco_box.x > max_x->deco_box.x) {
            max_x = iter;
        }
        if(iter->deco_box.y < min_y->deco_box.y) {
            min_y = iter;
        } else if(iter->deco_box.y > max_y->deco_box.y) {
            max_y = iter;
        }
    }

    switch(side) {
        case DIRECTION_UP:
            return min_y;
        case DIRECTION_DOWN:
            return max_y;
        case DIRECTION_LEFT:
            return min_x;
        case DIRECTION_RIGHT:
            return max_x;
    }
}

void
workspace_toplevels_set_enabled(struct workspace *workspace, bool enabled) {
    struct toplevel *iter;
    wl_list_for_each(iter, &workspace->masters, link) {
        wlr_scene_node_set_enabled(&iter->scene_tree->node, enabled);
    }

    wl_list_for_each(iter, &workspace->slaves, link) {
        wlr_scene_node_set_enabled(&iter->scene_tree->node, enabled);
    }

    wl_list_for_each(iter, &workspace->floating, link) {
        wlr_scene_node_set_enabled(&iter->scene_tree->node, enabled);
    }
}

void
workspace_set_master_ratio(struct workspace *workspace, double master_ratio) {
    workspace->master_ratio = clamp(master_ratio, 0.0, 1.0);

    layout_configure(workspace);
}
