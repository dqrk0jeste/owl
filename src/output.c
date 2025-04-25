#include "output.h"

#include <assert.h>
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
#include "ipc.h"
#include "keybinds.h"
#include "layout.h"
#include "mwc.h"
#include "rendering.h"
#include "toplevel.h"
#include "workspace.h"

extern struct server server;

static void
output_transfer_existing_workspaces(struct output *output) {
    // if this output is reconnected then its workspaces are on some other monitor,
    // we try to find it. we iterate through all the workspaces and compare its `original_output`
    // field with this outputs name
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

                if(output->active_workspace == NULL) {
                    output->active_workspace = iter_workspace;
                }
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
output_create_blur(struct output *output) {
    assert(output->blur == NULL);

    output->blur = wlr_scene_optimized_blur_create(&server.scene->tree, 0, 0);
    wlr_scene_node_place_above(&output->blur->node, &server.background_tree->node);
}

static void
output_destroy_blur(struct output *output) {
    assert(output->blur != NULL);
    wlr_scene_node_destroy(&output->blur->node);
    output->blur = NULL;
}

static void
output_update_blur(struct output *output) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    wlr_scene_node_set_position(&output->blur->node, output_box.x, output_box.y);
    wlr_scene_optimized_blur_set_size(output->blur, output_box.width, output_box.height);
}

static void
workspace_create_for_output(struct output *output, uint32_t index) {
    struct workspace *workspace = calloc(1, sizeof(*workspace));

    wl_list_init(&workspace->floating);
    wl_list_init(&workspace->masters);
    wl_list_init(&workspace->slaves);

    workspace->output = output;
    workspace->index = index;
    workspace->master_ratio = server.config->master_ratio;
    workspace->original_output = strdup(output->wlr_output->name);

    // we attach the keybinds that the config specified
    for(struct keybind *iter = server.config->keybinds; iter <= array_last(server.config->keybinds); iter++) {
        // we didnt have information about what workspace this is going to be, so we only kept an index. now we replace
        // it with the actual workspace pointer
        if(iter->action == keybind_change_workspace && (uintptr_t)iter->args == workspace->index) {
            iter->args = workspace;
            iter->initialized = true;
        } else if(iter->action == keybind_move_to_workspace && (uintptr_t)iter->args == workspace->index) {
            iter->args = workspace;
            iter->initialized = true;
        }
    }

    wl_list_insert(&output->workspaces, &workspace->link);
}

static void
output_assign_workspaces(struct output *output) {
    wl_list_init(&output->workspaces);

    // we check if this output already has some workspaces created; this happens when this output gets reattached, since
    // we never destroy workspaces, but instead just transfer them to some other output and now want them back
    output_transfer_existing_workspaces(output);

    if(!wl_list_empty(&output->workspaces))
        return;

    // if the list is still empty then there the mentioned scenario did not happen, so we try and create workspaces from
    // the config
    for(struct workspace_config *iter = server.config->workspaces; iter <= array_last(server.config->workspaces);
            iter++) {
        if(strcmp(iter->output, output->wlr_output->name) == 0) {
            workspace_create_for_output(output, iter->index);
        }
    }

    if(!wl_list_empty(&output->workspaces))
        return;

    // if its still empty then there were no configured workspaces for this output, so we just create the default one
    // indexed with zero
    wlr_log(WLR_ERROR, "no workspace config specified for output %s; using the default one", output->wlr_output->name);
    workspace_create_for_output(output, 0);
}

static void
output_handle_frame(struct wl_listener *listener, void *data) {
    // this function is called every time an output is ready to display a frame
    struct output *output = wl_container_of(listener, output, frame);

    output_draw(output);
    wlr_scene_output_commit(output->scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(output->scene_output, &now);
}

// todo: figure out what we want to do with this
static void
output_handle_request_state(struct wl_listener *listener, void *data) {
    // this function is called when the backend requests a new state for
    // the output. for example, wayland and X11 backends request a new mode
    // when the output window is resized
    struct output *output = wl_container_of(listener, output, request_state);
    struct wlr_output_event_request_state *event = data;

    wlr_output_commit_state(output->wlr_output, event->state);
}

// if an output is destroyed we want to evacuate all of its workspaces to some
// other output. we assume you always have at least one output enabled!
static void
output_evacuate_workspaces(struct output *output) {
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
    // todo: check for layer surfaces here
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

// todo: handle this cleaner, we may also add listeners for the output destroy on layer surfaces
static void
output_handle_destroy(struct wl_listener *listener, void *data) {
    struct output *output = wl_container_of(listener, output, destroy);

    // we want to transfer all the workspaces to a new output;

    // todo: if this was the only output then idk what to do honestly, maybe have a temporary stash thats going to hold
    // them until some output is attached again?
    // todo: try to make this cleaner, and remove this server.running maybe
    // todo: extract this into a function and also disable the things in the scene
    if(server.running) {
        output_evacuate_workspaces(output);
        if(output->session_lock_rect != NULL) {
            wlr_scene_node_destroy(&output->session_lock_rect->node);
        }

        if(output->blur != NULL) {
            output_destroy_blur(output);
        }

    } else {
        // todo: maybe destroy the workspaces? this way we could also handle the case of no output
    }

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);

    wl_list_remove(&output->link);

    free(output);
}

static bool
output_apply_preffered_mode(struct wlr_output *wlr_output) {
    struct wlr_output_state state;
    wlr_output_state_init(&state);

    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if(mode == NULL) {
        wlr_log(WLR_ERROR, "output %s does not have a preffered mode", wlr_output->name);
        wlr_output_state_finish(&state);
        return false;
    }

    wlr_output_state_set_mode(&state, mode);

    if(!wlr_output_commit_state(wlr_output, &state)) {
        wlr_log(WLR_ERROR, "couldn't apply the preffered mode to the output %s", wlr_output->name);
        wlr_output_state_finish(&state);
        return false;
    }

    wlr_output_state_finish(&state);
    return true;
}

static struct output_mode_config *
output_find_mode_config_by_name(char *name) {
    for(struct output_mode_config *iter = server.config->output_modes; iter <= array_last(server.config->output_modes);
            iter++) {
        if(strcmp(iter->name, name) == 0) {
            return iter;
        }
    }

    return NULL;
}

// modesets this output using the provided mode from the config file. if there is none, or it cant be applied backs up
// to the preffered mode
bool
output_modeset(struct wlr_output *wlr_output) {
    wlr_log(WLR_INFO, "configuring output %s", wlr_output->name);

    struct output_mode_config *config = output_find_mode_config_by_name(wlr_output->name);

    if(config == NULL) {
        wlr_log(WLR_INFO, "output %s not specified in the config; trying the preffered mode.", wlr_output->name);
        return output_apply_preffered_mode(wlr_output);
    }

    // we try to find the closest supported mode for this output, that means:
    //     - same resolution
    //     - closest refresh rate
    // if there is none we take the prefered mode for the output
    struct wlr_output_mode *best_match = NULL;
    uint32_t best_match_diff = UINT32_MAX;

    struct wlr_output_mode *m;
    wl_list_for_each(m, &wlr_output->modes, link) {
        if(m->width == config->width && m->height == config->height &&
                abs((int32_t)m->refresh - (int32_t)config->refresh_rate) < best_match_diff) {
            best_match = m;
            best_match_diff = abs((int32_t)m->refresh - (int32_t)config->refresh_rate);
        }
    }

    if(best_match == NULL) {
        wlr_log(WLR_ERROR, "could not find the requested mode, backing to preffered");
        return output_apply_preffered_mode(wlr_output);
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    // we set the output scale
    wlr_output_state_set_scale(&state, config->scale);

    wlr_log(WLR_INFO, "modesetting output %s to %dx%d@%dmHz", wlr_output->name, best_match->width, best_match->height,
            best_match->refresh);

    // we set the mode and try to commit the state. it should not fail!
    wlr_output_state_set_mode(&state, best_match);

    if(!wlr_output_commit_state(wlr_output, &state)) {
        wlr_log(WLR_ERROR, "could not apply the requested mode to the output %s", wlr_output->name);
        wlr_output_state_finish(&state);
        return false;
    }

    wlr_output_state_finish(&state);
    return true;
}

void
output_configure_blur(struct output *output) {
    if(server.config->blur) {
        if(output->blur == NULL) {
            output_create_blur(output);
        }
        // now that we know the blur is there we update it
        output_update_blur(output);
    } else {
        if(output->blur != NULL) {
            output_destroy_blur(output);
        }
    }
}

static struct output_position_config *
output_find_position_config_by_name(char *name) {
    for(struct output_position_config *iter = server.config->output_positions;
            iter <= array_last(server.config->output_positions); iter++) {
        if(strcmp(iter->name, name) == 0) {
            return iter;
        }
    }

    return NULL;
}

void
output_place_in_layout(struct output *output) {
    struct output_position_config *config = output_find_position_config_by_name(output->wlr_output->name);
    struct wlr_output_layout_output *layout = config == NULL
            ? wlr_output_layout_add_auto(server.output_layout, output->wlr_output)
            : wlr_output_layout_add(server.output_layout, output->wlr_output, config->x, config->y);

    wlr_scene_output_layout_add_output(server.scene_layout, layout, output->scene_output);

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);
    output->usable_area = output_box;
}

void
server_handle_new_output(struct wl_listener *listener, void *data) {
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server.allocator, server.renderer);

    // we try to configure the output. if it fails we quit as there is no point in having an output with no image on it
    if(!output_modeset(wlr_output)) {
        wlr_log(WLR_ERROR, "couldn't set up output %s, skipping", wlr_output->name);
        return;
    }

    wlr_log(WLR_INFO, "successfully set up output %s", wlr_output->name);

    // allocates and configures our state for this output
    struct output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    // we keep a reference to our state in this free data field
    wlr_output->data = output;

    // attach listeners
    output->frame.notify = output_handle_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_handle_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_handle_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    // we initialize per output layers on this output
    wl_list_init(&output->layers.background);
    wl_list_init(&output->layers.bottom);
    wl_list_init(&output->layers.top);
    wl_list_init(&output->layers.overlay);

    // insert it into a list
    wl_list_insert(&server.outputs, &output->link);

    // then we handle the scene part
    output->scene_output = wlr_scene_output_create(server.scene, output->wlr_output);
    output_place_in_layout(output);
    output_configure_blur(output);

    // and create the workspaces to this output
    output_assign_workspaces(output);

    // we take the first workspace for the active one for this output
    struct workspace *first = wl_container_of(output->workspaces.next, first, link);
    output->active_workspace = first;

    // and for the globally active if there isnt one
    if(server.active_workspace == NULL) {
        server.active_workspace = first;
    }
}

void
cursor_jump_output(struct output *output) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    wlr_cursor_warp(server.cursor, NULL, output_box.x + output_box.width / 2.0, output_box.y + output_box.height / 2.0);
}

// todo: should we handle layer surfaces before toplevels? no! if they are exclusive than they already have focus, if on
// demand idc
void
focus_output(struct output *output) {
    if(server.lock != NULL) {
        if(!wl_list_empty(&server.lock->surfaces)) {
            struct lock_surface *l = wl_container_of(server.lock->surfaces.next, l, link);
            focus_lock_surface(l);
        }
        return;
    }

    struct toplevel *focus_next = NULL;
    struct workspace *workspace = output->active_workspace;

    if(workspace->fullscreen != NULL) {
        focus_next = workspace->fullscreen;
    } else if(server.focused_toplevel == NULL || !server.focused_toplevel->floating) {
        bool is_master = server.focused_toplevel != NULL ? toplevel_is_master(server.focused_toplevel) : true;
        focus_next = layout_find_closest_toplevel(output->active_workspace, is_master, side);
        // if there are no tiled toplevels we try floating
        if(focus_next == NULL) {
            focus_next = workspace_find_closest_floating_toplevel(output->active_workspace, side);
        }
    } else {
        focus_next = workspace_find_closest_floating_toplevel(output->active_workspace, side);
        // if there are no floating toplevels we try tiled
        if(focus_next == NULL) {
            focus_next = layout_find_closest_toplevel(output->active_workspace, true, side);
        }
    }

    server.active_workspace = workspace;
    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);

    if(focus_next == NULL) {
        unfocus_focused_toplevel();
        cursor_jump_output(output);
    } else {
        focus_toplevel(focus_next);
        cursor_jump_focused_toplevel();
    }
}

// todo: may replace this function with the wlroots alternative, as this one is quite hacky
struct output *
output_get_relative(struct output *output, enum direction direction) {
    struct wlr_box original_output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &original_output_box);

    original_output_box.width *= output->wlr_output->scale;
    original_output_box.height *= output->wlr_output->scale;

    uint32_t original_output_midpoint_x = original_output_box.x + original_output_box.width / 2;
    uint32_t original_output_midpoint_y = original_output_box.y + original_output_box.height / 2;

    struct output *o;
    wl_list_for_each(o, &server.outputs, link) {
        struct wlr_box output_box;
        wlr_output_layout_get_box(server.output_layout, o->wlr_output, &output_box);
        output_box.width *= o->wlr_output->scale;
        output_box.height *= o->wlr_output->scale;

        if(direction == DIRECTION_LEFT && original_output_box.x == output_box.x + output_box.width &&
                original_output_midpoint_y > output_box.y &&
                original_output_midpoint_y < output_box.y + output_box.height) {
            return o;
        } else if(direction == DIRECTION_RIGHT && original_output_box.x + original_output_box.width == output_box.x &&
                original_output_midpoint_y > output_box.y &&
                original_output_midpoint_y < output_box.y + output_box.height) {
            return o;
        } else if(direction == DIRECTION_UP && original_output_box.y == output_box.y + output_box.height &&
                original_output_midpoint_x > output_box.x &&
                original_output_midpoint_x < output_box.x + output_box.width) {
            return o;
        } else if(direction == DIRECTION_DOWN && original_output_box.y + original_output_box.height == output_box.y &&
                original_output_midpoint_x > output_box.x &&
                original_output_midpoint_x < output_box.x + output_box.width) {
            return o;
        }
    }

    return NULL;
}

struct wlr_box
output_create_centered_box(struct output *output, uint32_t width, uint32_t height) {
    return (struct wlr_box){
            .x = output->usable_area.x + (output->usable_area.width - width) / 2,
            .y = output->usable_area.y + (output->usable_area.height - height) / 2,
            .width = width,
            .height = height,
    };
}
