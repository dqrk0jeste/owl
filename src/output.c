#include "output.h"

#include <assert.h>
#include <limits.h>
#include <scenefx/types/wlr_scene.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "array.h"
#include "config.h"
#include "effects.h"
#include "ipc.h"
#include "layout.h"
#include "mwc.h"
#include "toplevel.h"
#include "workspace.h"

extern struct server server;

static void
transfer_existing_workspaces(struct output *output) {
    // if this output is reconnected then its workspaces are on some other monitor, we try to find it. we iterate
    // through all the workspaces and compare its `original_output` field with this outputs name
    struct output *iter_output;
    struct workspace *iter_workspace, *tmp;
    wl_list_for_each(iter_output, &server.outputs, link) {
        // we skip this one if in the list
        if(iter_output == output)
            continue;

        wl_list_for_each_safe(iter_workspace, tmp, &iter_output->workspaces, link) {
            if(strcmp(iter_workspace->original_output, output->wlr_output->name) == 0) {
                // note: this should have been done, keeping it for reference
                // fix that outputs state. todo: optimize this by moving it to the bottom, when the only
                // workspaces that are left should be either owned by this output or evacuated from some
                // other output, but, anyhow, changing to this workspace must be valid if(iter_workspace
                // == iter_output->active_workspace) {
                //     struct workspace *owned_workspace = output_find_owned_workspace(iter_output);
                //     // it should have had its own workspace
                //     assert(owned_workspace != NULL);
                //     change_workspace(owned_workspace, false);
                // }
                // transfer it to this output
                iter_workspace->output = output;
                wl_list_remove(&iter_workspace->link);
                wl_list_insert(&output->workspaces, &iter_workspace->link);
            }
        }

        // after we have moved all the workspaces from this output we need to patch its active workspace
        if(iter_output->active_workspace->output != iter_output) {
            assert(!wl_list_empty(&iter_output->workspaces));
            struct workspace *first = wl_container_of(iter_output->workspaces.next, first, link);
            change_workspace(first, false);
        }
    }
}

static void
workspace_create_for_output(struct output *output, int index, double master_ratio) {
    struct workspace *workspace = calloc(1, sizeof(*workspace));

    wl_list_init(&workspace->floating);
    wl_list_init(&workspace->masters);
    wl_list_init(&workspace->slaves);

    workspace->output = output;
    workspace->index = index;
    workspace->master_ratio = master_ratio;
    workspace->original_output = strdup(output->wlr_output->name);

    wl_list_insert(output->workspaces.prev, &workspace->link);
}

static void
assign_workspaces(struct output *output, struct output_config *config) {
    wl_list_init(&output->workspaces);

    // we check if this output already has some workspaces created; this happens when this output gets reattached, since
    // we never destroy workspaces, but instead just transfer them to some other output and now want them back
    transfer_existing_workspaces(output);
    if(!wl_list_empty(&output->workspaces))
        return;

    // if the list is still empty then there the mentioned scenario did not happen, so we create them according to
    // config. note: there is always a default config here, so after this we are guaranteed at least one workspace
    for(int i = 0; i < array_len(config->workspaces); i++) {
        workspace_create_for_output(output, config->workspaces[i], config->master_ratio);
    }
}

static void
handle_frame(struct wl_listener *listener, void *data) {
    // this function is called every time an output is ready to display a frame
    struct output *output = wl_container_of(listener, output, frame);

    output_draw(output);
    wlr_scene_output_commit(output->scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(output->scene_output, &now);
}

// if an output is destroyed we want to evacuate all of its workspaces to some other output. we assume you always have
// at least one output enabled!
static void
evacuate_workspaces(struct output *output) {
    struct wl_list *next = output->link.next;
    if(next == &server.outputs) {
        next = output->link.prev;
    }

    if(next == &server.outputs) {
        // this means there is no outputs left. we are crashing here, as i am not putting the energy into this case
        return;
    }

    struct output *new = wl_container_of(next, new, link);
    // if the currently focused toplevel is on this output then we need to move focus of that one
    if(server.focused_toplevel != NULL && server.focused_toplevel->workspace->output == output) {
        focus_output(new, DIRECTION_LEFT);
    }

    struct workspace *iter, *tmp;
    wl_list_for_each_safe(iter, tmp, &output->workspaces, link) {
        // we reparent those workspaces, but before that we disable all the toplevels on that workspace
        workspace_toplevels_set_enabled(iter, false);
        if(iter->fullscreen != NULL) {
            wlr_scene_node_set_enabled(&iter->fullscreen->scene_tree->node, false);
        }

        iter->output = new;
        wl_list_remove(&iter->link);
        wl_list_insert(&new->workspaces, &iter->link);

        layout_configure(iter);
    }
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct output *output = wl_container_of(listener, output, destroy);

    if(server.mode != SERVER_MODE_SHUTTING) {
        // we want to transfer all the workspaces to a new output;
        evacuate_workspaces(output);

        wlr_scene_node_destroy(&output->blur->node);
        wlr_scene_node_destroy(&output->session_lock_rect->node);
        // } else {
        // todo: maybe destroy the workspaces? this way we could also handle the case of no output
    }

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy.link);

    wl_list_remove(&output->link);

    free(output);
}

static uint32_t
get_config(const char *name, struct output_config *config) {
    uint32_t found = 0;
    for(struct output_config *iter = array_last(server.config->outputs); iter >= server.config->outputs; iter--) {
        if((iter->specified & OUTPUT_FIELD_MATCH_NAME) && strcmp(iter->name, name) != 0)
            continue;

        if(!(found & OUTPUT_FIELD_MODE) && (iter->specified & OUTPUT_FIELD_MODE)) {
            config->width = iter->width;
            config->height = iter->height;
            config->refresh = iter->refresh;
            found |= OUTPUT_FIELD_MODE;
        }
        if(!(found & OUTPUT_FIELD_POSITION) && (iter->specified & OUTPUT_FIELD_POSITION)) {
            config->x = iter->x;
            config->y = iter->y;
            found |= OUTPUT_FIELD_POSITION;
        }
        if(!(found & OUTPUT_FIELD_SCALE) && (iter->specified & OUTPUT_FIELD_SCALE)) {
            config->scale = iter->scale;
            found |= OUTPUT_FIELD_SCALE;
        }
        if(!(found & OUTPUT_FIELD_WORKSPACES) && (iter->specified & OUTPUT_FIELD_WORKSPACES)) {
            config->workspaces = iter->workspaces;
            found |= OUTPUT_FIELD_WORKSPACES;
        }
        if(!(found & OUTPUT_FIELD_MASTER_COUNT) && (iter->specified & OUTPUT_FIELD_MASTER_COUNT)) {
            config->master_count = iter->master_count;
            found |= OUTPUT_FIELD_MASTER_COUNT;
        }
        if(!(found & OUTPUT_FIELD_MASTER_RATIO) && (iter->specified & OUTPUT_FIELD_MASTER_RATIO)) {
            config->master_ratio = iter->master_ratio;
            found |= OUTPUT_FIELD_MASTER_RATIO;
        }
    }

    return found;
}

static void
output_apply_preffered_mode(struct output *output) {
    struct wlr_output_mode *mode = wlr_output_preferred_mode(output->wlr_output);
    if(mode == NULL) {
        wlr_log(WLR_ERROR, "output `%s` does not have a preffered mode", output->wlr_output->name);
        return;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    wlr_output_state_set_mode(&state, mode);

    if(!wlr_output_commit_state(output->wlr_output, &state)) {
        wlr_log(WLR_ERROR, "couldn't apply the preffered mode to the output `%s`", output->wlr_output->name);
    }

    wlr_output_state_finish(&state);
}

static void
modeset(struct output *output, int width, int height, int refresh, double scale) {
    wlr_log(WLR_INFO, "configuring output `%s`", output->wlr_output->name);

    if(width == 0) {
        wlr_log(WLR_INFO, "no mode specified for the output `%s`. trying the preffered mode.",
                output->wlr_output->name);
        output_apply_preffered_mode(output);
        return;
    }

    // we try to find the closest supported mode for this output, that means:
    //     - same resolution
    //     - closest refresh rate (since a lot of times its like 59.994 or something)
    // if there is none we take the prefered mode for the output
    struct wlr_output_mode *best = NULL;
    int diff = INT_MAX;

    struct wlr_output_mode *iter;
    wl_list_for_each(iter, &output->wlr_output->modes, link) {
        if(iter->width == width && iter->height == height && abs(iter->refresh - refresh) < diff) {
            best = iter;
            diff = abs(iter->refresh - refresh);
        }
    }

    if(best == NULL) {
        wlr_log(WLR_ERROR, "output does not support the requested mode, backing to preffered");
        output_apply_preffered_mode(output);
        return;
    }

    wlr_log(WLR_INFO, "modesetting output `%s` to %dx%d@%dmHz", output->wlr_output->name, best->width, best->height,
            best->refresh);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    wlr_output_state_set_scale(&state, scale);
    wlr_output_state_set_mode(&state, best);

    // try to commit the state. it should not fail!
    if(!wlr_output_commit_state(output->wlr_output, &state)) {
        wlr_log(WLR_ERROR, "could not apply the requested mode to the output `%s`", output->wlr_output->name);
    }

    wlr_output_state_finish(&state);
}

void
output_configure(struct output *output, bool initial) {
    struct output_config config;
    uint32_t found = get_config(output->wlr_output->name, &config);

    // there is always at least the default configuration
    modeset(output, config.width, config.height, config.refresh, config.scale);

    // place it it the layout; todo: fix this
    struct wlr_output_layout_output *layout = found & OUTPUT_FIELD_POSITION
            ? wlr_output_layout_add(server.output_layout, output->wlr_output, config.x, config.y)
            : wlr_output_layout_add_auto(server.output_layout, output->wlr_output);

    wlr_scene_output_layout_add_output(server.scene_layout, layout, output->scene_output);

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);
    output->usable_area = output_box;

    output->master_count = config.master_count;

    wlr_scene_node_set_position(&output->blur->node, output_box.x, output_box.y);
    wlr_scene_optimized_blur_set_size(output->blur, output_box.width, output_box.height);
    wlr_scene_node_set_enabled(&output->blur->node, server.config->needs_optimized_blur);

    wlr_scene_node_set_position(&output->session_lock_rect->node, output_box.x, output_box.y);
    wlr_scene_rect_set_size(output->session_lock_rect, output_box.width, output_box.height);

    if(initial) {
        assign_workspaces(output, &config);
    }
}

void
handle_new_output(struct wl_listener *listener, void *data) {
    struct wlr_output *wlr_output = data;

    // allocates and configures our state for this output
    struct output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    // we keep a reference to our state in this free data field
    wlr_output->data = output;

    // insert it into a list
    wl_list_insert(&server.outputs, &output->link);

    wlr_output_init_render(wlr_output, server.allocator, server.renderer);
    output->scene_output = wlr_scene_output_create(server.scene, output->wlr_output);

    // create blur and session lock rect for this output;
    output->blur = wlr_scene_optimized_blur_create(&server.scene->tree, 0, 0);
    wlr_scene_node_place_above(&output->blur->node, &server.background_tree->node);

    output->session_lock_rect = wlr_scene_rect_create(&server.scene->tree, 0, 0, (float[4]){0.0f, 0.0f, 0.0f, 1.0f});
    wlr_scene_node_place_above(&output->session_lock_rect->node, &server.overlay_tree->node);
    wlr_scene_node_set_enabled(&output->session_lock_rect->node, server.mode == SERVER_MODE_LOCKED);

    output_configure(output, true);

    // we take the first workspace for the active one for this output
    struct workspace *first = wl_container_of(output->workspaces.next, first, link);
    output->active_workspace = first;

    // and for the globally active if there isnt one
    if(server.active_workspace == NULL) {
        server.active_workspace = first;
    }

    // we initialize per output layers on this output
    wl_list_init(&output->layers.background);
    wl_list_init(&output->layers.bottom);
    wl_list_init(&output->layers.top);
    wl_list_init(&output->layers.overlay);

    // attach listeners
    output->frame.notify = handle_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->destroy.notify = handle_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
}

void
jump_cursor_to_output(struct output *output) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    wlr_cursor_warp(server.cursor.base, NULL, output_box.x + output_box.width / 2.0,
            output_box.y + output_box.height / 2.0);
}

void
focus_output(struct output *output, enum direction direction) {
    struct workspace *workspace = output->active_workspace;
    server.active_workspace = workspace;
    ipc_send_active_workspace();

    if(server.mode == SERVER_MODE_LOCKED) {
        if(!wl_list_empty(&server.lock_manager.current_lock->surfaces)) {
            struct lock_surface *first = wl_container_of(server.lock_manager.current_lock->surfaces.next, first, link);
            focus_lock_surface(first);
        }
        return;
    }

    if(server.mode > SERVER_MODE_CAN_GIVE_FOCUS || server.exclusive)
        return;

    if(workspace->fullscreen != NULL) {
        focus_toplevel(workspace->fullscreen, true);
    } else if(has_floating(workspace)) {
        focus_toplevel(workspace_find_closest_floating(workspace, opposite(direction)), true);
    } else if(has_masters(workspace)) {
        focus_toplevel(first_master(workspace), true);
    } else {
        unfocus_focused_toplevel();
        jump_cursor_to_output(output);
    }
}

static int wlr_direction_from[] = {
        [DIRECTION_UP] = WLR_DIRECTION_UP,
        [DIRECTION_RIGHT] = WLR_DIRECTION_RIGHT,
        [DIRECTION_DOWN] = WLR_DIRECTION_DOWN,
        [DIRECTION_LEFT] = WLR_DIRECTION_LEFT,
};

struct output *
output_get_relative(struct output *output, enum direction direction, int x, int y) {
    struct wlr_output *wlr_output = wlr_output_layout_adjacent_output(server.output_layout,
            wlr_direction_from[direction], output->wlr_output, x, y);
    if(wlr_output == NULL)
        return NULL;

    return wlr_output->data;
}

struct wlr_box
output_create_centered_box(struct output *output, int width, int height) {
    return (struct wlr_box){
            .x = output->usable_area.x + (output->usable_area.width - width) / 2,
            .y = output->usable_area.y + (output->usable_area.height - height) / 2,
            .width = width,
            .height = height,
    };
}
