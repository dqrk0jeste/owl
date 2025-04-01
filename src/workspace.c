#include <scenefx/types/wlr_scene.h>

#include "workspace.h"

#include "layout.h"
#include "mwc.h"
#include "ipc.h"
#include "keybinds.h"
#include "layer_surface.h"
#include "something.h"

#include <assert.h>
#include <stdlib.h>

extern struct mwc_server server;

void
workspace_create_for_output(struct mwc_output *output, struct workspace_config *config) {
    struct mwc_workspace *workspace = calloc(1, sizeof(*workspace));

    wl_list_init(&workspace->floating_toplevels);
    wl_list_init(&workspace->masters);
    wl_list_init(&workspace->slaves);

    workspace->output = output;
    workspace->index = config->index;
    workspace->config = config;

    wl_list_insert(&output->workspaces, &workspace->link);

    /* if first then set it active */
    if(output->active_workspace == NULL) {
        output->active_workspace = workspace;
    }

    struct keybind *k;
    wl_list_for_each(k, &server.config->keybinds, link) {
        /* we didnt have information about what workspace this is going to be,
     * so we only kept an index. now we replace it with
     * the actual workspace pointer */
        if(k->action == keybind_change_workspace && (uint64_t)k->args == workspace->index) {
            k->args = workspace;
            k->initialized = true;
        } else if(k->action == keybind_move_focused_toplevel_to_workspace
            && (uint64_t)k->args == workspace->index) {
            k->args = workspace;
            k->initialized = true;
        }
    }
}

void
change_workspace(struct mwc_workspace *workspace, bool keep_focus) {
    /* if it is the same as global active workspace, do nothing */
    if(server.active_workspace == workspace) return;

    /* if it is an already active on its output, just switch to it */
    if(workspace == workspace->output->active_workspace) {
        if(keep_focus) {
            /* do nothing */
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

    /* else remove all the toplevels on that workspace */
    struct mwc_toplevel *t;
    wl_list_for_each(t, &workspace->output->active_workspace->floating_toplevels, link) {
        wlr_scene_node_set_enabled(&t->scene_tree->node, false);
    }
    wl_list_for_each(t, &workspace->output->active_workspace->masters, link) {
        wlr_scene_node_set_enabled(&t->scene_tree->node, false);
    }
    wl_list_for_each(t, &workspace->output->active_workspace->slaves, link) {
        wlr_scene_node_set_enabled(&t->scene_tree->node, false);
    }

    /* and show this workspace's toplevels */
    if(workspace->fullscreen_toplevel != NULL) {
        wlr_scene_node_set_enabled(&workspace->fullscreen_toplevel->scene_tree->node, true);
        layers_under_fullscreen_set_enabled(workspace->output, false);
    } else {
        wl_list_for_each(t, &workspace->floating_toplevels, link) {
            wlr_scene_node_set_enabled(&t->scene_tree->node, true);
        }
        wl_list_for_each(t, &workspace->masters, link) {
            wlr_scene_node_set_enabled(&t->scene_tree->node, true);
        }
        wl_list_for_each(t, &workspace->slaves, link) {
            wlr_scene_node_set_enabled(&t->scene_tree->node, true);
        }

        if(workspace->output->active_workspace->fullscreen_toplevel != NULL) {
            layers_under_fullscreen_set_enabled(workspace->output, true);
        }
    }

    if(server.active_workspace->output != workspace->output) {
        cursor_jump_output(workspace->output);
    }

    server.active_workspace = workspace;
    workspace->output->active_workspace = workspace;
    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);

    /* same as above */
    if(keep_focus) {
        /* do nothing */
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
}

void
toplevel_move_to_workspace(struct mwc_toplevel *toplevel,
                           struct mwc_workspace *workspace) {
    assert(toplevel != NULL && workspace != NULL);
    if(toplevel == server.grabbed_toplevel || toplevel->workspace == workspace
        || workspace->fullscreen_toplevel != NULL) return;

    struct mwc_workspace *old_workspace = toplevel->workspace;

    /* handle server state; note: even tho fullscreen toplevel is handled differently
   * we will still update its underlying type */
    if(toplevel->floating) {
        toplevel->workspace = workspace;
        wl_list_remove(&toplevel->link);
        wl_list_insert(&workspace->floating_toplevels, &toplevel->link);
    } else if(toplevel_is_master(toplevel)){
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

    /* handle presentation */
    if(toplevel->fullscreen) {
        old_workspace->fullscreen_toplevel = NULL;
        workspace->fullscreen_toplevel = toplevel;

        struct wlr_box output_box;
        wlr_output_layout_get_box(server.output_layout, workspace->output->wlr_output, &output_box);
        toplevel_set_pending_state(toplevel, output_box.x, output_box.y,
                                   output_box.width, output_box.height);

        layers_under_fullscreen_set_enabled(workspace->output, false);
        if(old_workspace->output != workspace->output) {
            layers_under_fullscreen_set_enabled(old_workspace->output, true);
        }

        if(toplevel->floating) {
            /* calculate where the toplevel should be placed after exiting fullscreen,
       * see note for floating bellow */
            uint32_t old_output_relative_x =
                toplevel->prev_geometry.x - old_workspace->output->usable_area.x;
            double relative_x =
                (double)old_output_relative_x / old_workspace->output->usable_area.width;

            uint32_t old_output_relative_y =
                toplevel->prev_geometry.y - old_workspace->output->usable_area.y;
            double relative_y =
                (double)old_output_relative_y / old_workspace->output->usable_area.height;

            uint32_t new_output_x = workspace->output->usable_area.x
                + relative_x * workspace->output->usable_area.width;
            uint32_t new_output_y = workspace->output->usable_area.y
                + relative_y * workspace->output->usable_area.height;

            toplevel->prev_geometry.x = new_output_x;
            toplevel->prev_geometry.y = new_output_y;
        } else {
            layout_set_pending_state(old_workspace);
        }
    } else if(toplevel->floating && old_workspace->output != workspace->output) {
        /* we want to place the toplevel to the same relative coordinates,
     * as the new output may have a different resolution */
        uint32_t old_output_relative_x =
            toplevel->scene_tree->node.x - old_workspace->output->usable_area.x;
        double relative_x =
            (double)old_output_relative_x / old_workspace->output->usable_area.width;

        uint32_t old_output_relative_y =
            toplevel->scene_tree->node.y - old_workspace->output->usable_area.y;
        double relative_y =
            (double)old_output_relative_y / old_workspace->output->usable_area.height;

        uint32_t new_output_x = workspace->output->usable_area.x
            + relative_x * workspace->output->usable_area.width;
        uint32_t new_output_y = workspace->output->usable_area.y
            + relative_y * workspace->output->usable_area.height;

        // FIXME: also use relative size
        toplevel_set_pending_state(toplevel, new_output_x, new_output_y,
                                   toplevel->container_current.width, toplevel->container_current.height);
    } else {
        layout_set_pending_state(old_workspace);
        layout_set_pending_state(workspace);
    }

    /* change active workspace */
    change_workspace(workspace, true);
}

struct mwc_toplevel *
workspace_find_closest_floating_toplevel(struct mwc_workspace *workspace,
                                         enum mwc_direction side) {
    struct wl_list *l = workspace->floating_toplevels.next;
    if(l == &workspace->floating_toplevels) return NULL;

    struct mwc_toplevel *t = wl_container_of(l, t, link);

    struct mwc_toplevel *min_x = t;
    struct mwc_toplevel *max_x = t;
    struct mwc_toplevel *min_y = t;
    struct mwc_toplevel *max_y = t;

    wl_list_for_each(t, &workspace->floating_toplevels, link) {
        if(X(t) < X(min_x)) {
            min_x = t;
        } else if(X(t) > X(max_x)) {
            max_x = t;
        }
        if(Y(t) < Y(min_y)) {
            min_y = t;
        } else if(Y(t) > Y(max_y)) {
            max_y = t;
        }
    }

    switch(side) {
        case MWC_UP: return min_y;
        case MWC_DOWN: return max_y;
        case MWC_LEFT: return min_x;
        case MWC_RIGHT: return max_x;
    }
}

