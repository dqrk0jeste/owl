#include "workspace.h"

#include <assert.h>
#include <scenefx/types/wlr_scene.h>
#include <stdint.h>

#include "ipc.h"
#include "layer_surface.h"
#include "layout.h"
#include "mwc.h"
#include "view.h"

extern struct mwc_server server;

void
change_workspace(struct mwc_workspace *workspace, bool keep_focus) {
    // if it is the same as global active workspace, do nothing
    if(server.active_workspace == workspace) return;

    // if it is an already active on its output, just switch to it
    if(workspace == workspace->output->active_workspace) {
        if(keep_focus) {
            // do nothing
        } else if(workspace->fullscreen_toplevel != NULL) {
            focus_toplevel(workspace->fullscreen_toplevel);
        } else if(!wl_list_empty(&workspace->masters)) {
            struct mwc_toplevel *t = wl_container_of(workspace->masters.next, t, link);
            focus_toplevel(t);
        } else if(!wl_list_empty(&workspace->floating_toplevels)) {
            struct mwc_toplevel *t = wl_container_of(workspace->floating_toplevels.next, t, link);
            focus_toplevel(t);
        } else {
            unfocus_focused_toplevel();
        }

        server.active_workspace = workspace;
        cursor_jump_output(workspace->output);
        ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);
        return;
    }

    struct mwc_workspace *current_workspace = workspace->output->active_workspace;
    // else remove all the toplevels on the current workspace
    workspace_toplevels_set_enabled(current_workspace, false);

    if(workspace->fullscreen_toplevel != NULL) {
        // if there is a fullscreen toplevel we only render it and disable all the other things
        wlr_scene_node_set_enabled(&workspace->fullscreen_toplevel->scene_tree->node, true);
        layers_under_fullscreen_set_enabled(workspace->output, false);
    } else {
        // else enable all of the toplevels and layers (they may have been disabled if `current_workspace` had a
        // fullscreen toplevel on it)
        workspace_toplevels_set_enabled(workspace, true);
        layers_under_fullscreen_set_enabled(workspace->output, true);
    }

    // also warp the cursor if this output is not on the same output as currently globally active workspace
    if(server.active_workspace->output != workspace->output) {
        cursor_jump_output(workspace->output);
    }

    // set it as globally active workspace
    server.active_workspace = workspace;
    // and also as this outputs active workspace
    workspace->output->active_workspace = workspace;

    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);

    // handle the focus change
    if(keep_focus) {
        // do nothing
    } else if(workspace->fullscreen_toplevel != NULL) {
        focus_toplevel(workspace->fullscreen_toplevel);
    } else if(keep_focus) {
        return;
    } else if(!wl_list_empty(&workspace->masters)) {
        struct mwc_toplevel *t = wl_container_of(workspace->masters.next, t, link);
        focus_toplevel(t);
    } else if(!wl_list_empty(&workspace->floating_toplevels)) {
        struct mwc_toplevel *t = wl_container_of(workspace->floating_toplevels.next, t, link);
        focus_toplevel(t);
    } else {
        unfocus_focused_toplevel();
    }

    // we also handle pointer focus
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    pointer_handle_focus(timespec_to_ms(&now), false);
}

void
toplevel_move_to_workspace(struct mwc_toplevel *toplevel, struct mwc_workspace *workspace) {
    if(toplevel == server.grabbed_toplevel || toplevel->workspace == workspace ||
            workspace->fullscreen_toplevel != NULL)
        return;

    struct mwc_workspace *old_workspace = toplevel->workspace;

    // handle server state; note: even tho fullscreen toplevel is handled
    // differently we will still update its underlying type (tiled or floating)
    if(toplevel->floating) {
        toplevel->workspace = workspace;
        wl_list_remove(&toplevel->link);
        wl_list_insert(&workspace->floating_toplevels, &toplevel->link);
    } else if(toplevel_is_master(toplevel)) {
        wl_list_remove(&toplevel->link);
        if(!wl_list_empty(&old_workspace->slaves)) {
            struct mwc_toplevel *s = wl_container_of(old_workspace->slaves.next, s, link);
            wl_list_remove(&s->link);
            wl_list_insert(old_workspace->masters.prev, &s->link);
        }

        toplevel->workspace = workspace;
        if(wl_list_length(&workspace->masters) < server.config->master_count) {
            wl_list_insert(workspace->masters.prev, &toplevel->link);
        } else {
            wl_list_insert(workspace->slaves.prev, &toplevel->link);
        }
    } else {
        wl_list_remove(&toplevel->link);

        toplevel->workspace = workspace;
        if(wl_list_length(&workspace->masters) < server.config->master_count) {
            wl_list_insert(workspace->masters.prev, &toplevel->link);
        } else {
            wl_list_insert(workspace->slaves.prev, &toplevel->link);
        }
    }

    // handle presentation
    if(toplevel->fullscreen) {
        old_workspace->fullscreen_toplevel = NULL;
        workspace->fullscreen_toplevel = toplevel;

        struct wlr_box output_box;
        wlr_output_layout_get_box(server.output_layout, workspace->output->wlr_output, &output_box);
        toplevel_set_state(toplevel, output_box);

        // if the output changed then we enable the layers on the old output, and disable it on this one
        if(old_workspace->output != workspace->output) {
            layers_under_fullscreen_set_enabled(old_workspace->output, true);
            layers_under_fullscreen_set_enabled(workspace->output, false);
        }

        if(toplevel->floating) {
            // calculate where the toplevel should be placed after exiting fullscreen; we use the same relative place on
            // this output as is was on the last one
            int32_t old_output_relative_x = toplevel->prev_deco_box.x - old_workspace->output->usable_area.x;
            double relative_x = (double)old_output_relative_x / old_workspace->output->usable_area.width;

            int32_t old_output_relative_y = toplevel->prev_deco_box.y - old_workspace->output->usable_area.y;
            double relative_y = (double)old_output_relative_y / old_workspace->output->usable_area.height;

            int32_t new_output_x = workspace->output->usable_area.x + relative_x * workspace->output->usable_area.width;
            int32_t new_output_y =
                    workspace->output->usable_area.y + relative_y * workspace->output->usable_area.height;

            // patch `prev_deco_box` with these new values
            toplevel->prev_deco_box.x = new_output_x;
            toplevel->prev_deco_box.y = new_output_y;
        } else {
            layout_configure(old_workspace);
        }
    } else if(toplevel->floating && old_workspace->output != workspace->output) {
        // if the toplevel is moved between workspaces on the same output we dont do anything about the presentation.
        // else we place it to the same relative coords on the new output
        int32_t old_output_relative_x = toplevel->deco_box.x - old_workspace->output->usable_area.x;
        double relative_x = (double)old_output_relative_x / old_workspace->output->usable_area.width;

        int32_t old_output_relative_y = toplevel->deco_box.y - old_workspace->output->usable_area.y;
        double relative_y = (double)old_output_relative_y / old_workspace->output->usable_area.height;

        int32_t new_output_x = workspace->output->usable_area.x + relative_x * workspace->output->usable_area.width;
        int32_t new_output_y = workspace->output->usable_area.y + relative_y * workspace->output->usable_area.height;

        toplevel_set_state(toplevel,
                (struct wlr_box){new_output_x, new_output_y, toplevel->deco_box.width, toplevel->deco_box.height});
    } else {
        // and if tiled we just configure the layouts of both the old one a the new one
        layout_configure(old_workspace);
        layout_configure(workspace);
    }

    // change active workspace, but keep the focus unchanged
    change_workspace(workspace, true);
}

struct mwc_toplevel *
workspace_find_closest_floating_toplevel(struct mwc_workspace *workspace, enum mwc_direction side) {
    if(wl_list_empty(&workspace->floating_toplevels)) return NULL;

    struct mwc_toplevel *first = wl_container_of(workspace->floating_toplevels.next, first, link);
    struct mwc_toplevel *min_x = first, *max_x = first, *min_y = first, *max_y = first;

    struct mwc_toplevel *iter;
    wl_list_for_each(iter, &workspace->floating_toplevels, link) {
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
        case MWC_UP:
            return min_y;
        case MWC_DOWN:
            return max_y;
        case MWC_LEFT:
            return min_x;
        case MWC_RIGHT:
            return max_x;
    }
}

void
workspace_toplevels_set_enabled(struct mwc_workspace *workspace, bool enabled) {
    struct mwc_toplevel *iter;
    wl_list_for_each(iter, &workspace->masters, link) {
        wlr_scene_node_set_enabled(&iter->scene_tree->node, enabled);
    }

    wl_list_for_each(iter, &workspace->slaves, link) {
        wlr_scene_node_set_enabled(&iter->scene_tree->node, enabled);
    }

    wl_list_for_each(iter, &workspace->floating_toplevels, link) {
        wlr_scene_node_set_enabled(&iter->scene_tree->node, enabled);
    }
}
