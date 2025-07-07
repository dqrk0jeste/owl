#include "workspace.h"

#include <assert.h>
#include <limits.h>
#include <scenefx/types/wlr_scene.h>
#include <stdint.h>

#include "helpers.h"
#include "ipc.h"
#include "layer_shell.h"
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

    // if we were resizing the layout stop it before switching to new workspace
    if(server.mode == SERVER_MODE_RESIZING_MASTER_RATIO) {
        cursor_stop_move_resize();
    }

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
        cursor_warp_output(workspace->output);
    }

    // set it as globally active workspace
    server.active_workspace = workspace;
    // and also as this outputs active workspace
    workspace->output->active_workspace = workspace;

    ipc_send_active_workspace();

    // handle the keyboard focus
    if(!keep_focus && server.mode <= SERVER_MODE_CAN_GIVE_FOCUS && !server.exclusive) {
        handle_focus(workspace);
    }

    // and pointer focus
    cursor_handle_focus(get_now_in_ms(), false);
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
        if(toplevel->mode == TOPLEVEL_MODE_MASTER) {
            old_workspace->master_count--;
            if(has_slaves(old_workspace)) {
                promote_last_slave(old_workspace);
            }
        } else {
            old_workspace->slave_count--;
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

        if(old_workspace->output != workspace->output) {
            // if the output changed then we enable the layers and toplevels on the old output
            layers_under_fullscreen_set_enabled(old_workspace->output, true);
            workspace_toplevels_set_enabled(old_workspace, true);
            // and disable them on this one
            layers_under_fullscreen_set_enabled(workspace->output, false);
            workspace_toplevels_set_enabled(workspace, false);
        }

        if(toplevel->prev_mode == TOPLEVEL_MODE_FLOATING && old_workspace->output != workspace->output) {
            // calculate where the toplevel should be placed after exiting fullscreen; we use the same relative place on
            // this output as is was on the last one
            get_same_relative_coords(&toplevel->prev_deco_box.x, &toplevel->prev_deco_box.y,
                    &old_workspace->output->usable_area, &workspace->output->usable_area);
        } else {
            // invalidate the index
            toplevel->prev_index = -1;
            layout_configure(old_workspace);
        }
    } else if(toplevel->mode == TOPLEVEL_MODE_FLOATING && old_workspace->output != workspace->output) {
        // if the toplevel is moved between workspaces on the same output we dont do anything about the presentation;
        // else we place it at the same relative coords on the new output; here we copy the box, since this state should
        // not be changed directly
        struct wlr_box box = toplevel->deco_box;
        get_same_relative_coords(&box.x, &box.y, &old_workspace->output->usable_area, &workspace->output->usable_area);
        toplevel_set_state(toplevel, box);
    } else if(toplevel_is_tiled(toplevel)) {
        // and if tiled we just configure the layouts of both the old one and the new one
        layout_configure(old_workspace);
        layout_configure(workspace);
    }
}

struct toplevel *
workspace_find_closest_floating(struct workspace *workspace, enum direction side) {
    if(!has_floating(workspace))
        return NULL;

    if(side == DIRECTION_UP) {
        struct toplevel *min = NULL;
        int min_val = INT_MAX;

        struct toplevel *iter;
        wl_list_for_each(iter, &workspace->floating, link) {
            int y = iter->deco_box.y + iter->deco_box.height / 2;

            if(y < min_val) {
                min = iter;
                min_val = y;
            }
        }

        return min;
    } else if(side == DIRECTION_DOWN) {
        struct toplevel *max = NULL;
        int max_val = INT_MIN;

        struct toplevel *iter;
        wl_list_for_each(iter, &workspace->floating, link) {
            int y = iter->deco_box.y + iter->deco_box.height / 2;

            if(y > max_val) {
                max = iter;
                max_val = y;
            }
        }

        return max;
    } else if(side == DIRECTION_LEFT) {
        struct toplevel *min = NULL;
        int min_val = INT_MAX;

        struct toplevel *iter;
        wl_list_for_each(iter, &workspace->floating, link) {
            int x = iter->deco_box.x + iter->deco_box.width / 2;

            if(x < min_val) {
                min = iter;
                min_val = x;
            }
        }

        return min;
    } else if(side == DIRECTION_RIGHT) {
        struct toplevel *max = NULL;
        int max_val = INT_MIN;

        struct toplevel *iter;
        wl_list_for_each(iter, &workspace->floating, link) {
            int x = iter->deco_box.x + iter->deco_box.width / 2;

            if(x > max_val) {
                max = iter;
                max_val = x;
            }
        }

        return max;
    }

    assert(false && "unreachable");
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
    workspace->master_ratio = clamp(master_ratio, 0.05, 0.95);

    layout_configure(workspace);
}

struct workspace *
workspace_find_by_index(int index) {
    struct output *iter_output;
    wl_list_for_each(iter_output, &server.outputs, link) {
        struct workspace *iter_workspace;
        wl_list_for_each(iter_workspace, &iter_output->workspaces, link) {
            if(iter_workspace->index == index)
                return iter_workspace;
        }
    }

    return NULL;
}
