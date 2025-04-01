#include <scenefx/types/wlr_scene.h>
#include "output.h"

#include "mwc.h"
#include "config.h"
#include "layout.h"
#include "pointer.h"
#include "rendering.h"
#include "workspace.h"
#include "toplevel.h"
#include "ipc.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_cursor.h>

extern struct mwc_server server;

void
server_handle_new_output(struct wl_listener *listener, void *data) {
    struct wlr_output *wlr_output = data;

    /* we try to find the config for this output */
    struct output_config *output_config = NULL;

    struct output_config *o;
    wl_list_for_each(o, &server.config->outputs, link) {
        if(strcmp(o->name, wlr_output->name) == 0) {
            output_config = o;
            break;
        }
    }

    bool success = output_initialize(wlr_output, output_config);
    if(!success) return;

    /* allocates and configures our state for this output */
    struct mwc_output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;

    wlr_output->data = output;

    output->frame.notify = output_handle_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_handle_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_handle_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_init(&output->workspaces);

    /* we check if this output already has some workspaces created */
    bool found = output_transfer_existing_workspaces(output);
    if(!found) {
        struct workspace_config *c;
        /* we go in reverse to first add workspaces that were on top of config */
        wl_list_for_each_reverse(c, &server.config->workspaces, link) {
            if(strcmp(c->output, wlr_output->name) == 0) {
                workspace_create_for_output(output, c);
            }
        }
    }

    /* if we didnt find any workspace config, then we give it workspace with index 0 */
    if(wl_list_empty(&output->workspaces)) {
        wlr_log(WLR_ERROR, "no workspace config specified for output %s."
                "using default workspace 0. please add a valid workspace config.",
                wlr_output->name);

        struct mwc_workspace *workspace = calloc(1, sizeof(*workspace));
        wl_list_init(&workspace->floating_toplevels);
        wl_list_init(&workspace->masters);
        wl_list_init(&workspace->slaves);
        workspace->output = output;
        workspace->index = 0;

        wl_list_insert(&output->workspaces, &workspace->link);

        output->active_workspace = workspace;
    }

    wl_list_init(&output->layers.background);
    wl_list_init(&output->layers.bottom);
    wl_list_init(&output->layers.top);
    wl_list_init(&output->layers.overlay);

    wl_list_insert(&server.outputs, &output->link);

    output->scene_output = wlr_scene_output_create(server.scene, output->wlr_output);
    struct wlr_box output_box = output_add_to_layout(output, output_config);

    /* if there were some existing workspaces then we reconfigure them */
    if(found) {
        struct mwc_workspace *w;
        wl_list_for_each(w, &output->workspaces, link) {
            layout_set_pending_state(w);
            /* this pathces some ghosts that might have been left in the scene */
            if(w != output->active_workspace) {
                struct mwc_toplevel *t;
                wl_list_for_each(t, &w->floating_toplevels, link) {
                    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
                }
                wl_list_for_each(t, &w->masters, link) {
                    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
                }
                wl_list_for_each(t, &w->slaves, link) {
                    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
                }
            }
        }
    }

    if(server.config->blur) {
        output->blur = wlr_scene_optimized_blur_create(&server.scene->tree,
                                                       wlr_output->width, wlr_output->height);
        wlr_scene_set_blur_data(server.scene, server.config->blur_params);
        wlr_scene_node_place_above(&output->blur->node, &server.background_tree->node);
        wlr_scene_node_set_position(&output->blur->node, output_box.x, output_box.y);
    }

    /* if first output then set server's active workspace to this one */
    if(server.active_workspace == NULL) {
        server.active_workspace = output->active_workspace;
    }
}

struct mwc_workspace *
output_find_owned_workspace(struct mwc_output *output) {
    struct mwc_workspace *w;
    wl_list_for_each(w, &output->workspaces, link) {
        if(strcmp(w->config->output, output->wlr_output->name) == 0) {
            return w;
        }
    }

    return NULL;
}

bool
output_transfer_existing_workspaces(struct mwc_output *output) {
    /* if this output is reconnected then its workspaces are on some other monitor,
   * we try to find it; this is not efficient as things could be flagged, i am just lazy rn */
    bool found = false;
    struct mwc_output *o;
    struct mwc_workspace *w, *tmp;
    wl_list_for_each(o, &server.outputs, link) {
        wl_list_for_each_safe(w, tmp, &o->workspaces, link) {
            if(w->config != NULL && strcmp(w->config->output, output->wlr_output->name) == 0) {
                /* fix that outputs state */
                if(w == o->active_workspace) {
                    struct mwc_workspace *owned_workspace = output_find_owned_workspace(o);
                    /* it should have had its own workspace */
                    assert(owned_workspace != NULL);
                    change_workspace(owned_workspace, false);
                }
                /* transfer it to this output */
                w->output = output;
                wl_list_remove(&w->link);
                wl_list_insert(&output->workspaces, &w->link);
                if(output->active_workspace == NULL) {
                    output->active_workspace = w;
                }
                found = true;
            }
        }
    }

    return found;

}

struct wlr_box
output_add_to_layout(struct mwc_output *output, struct output_config *config) {
    struct wlr_output_layout_output *layout;
    if(config != NULL) {
        wlr_log(WLR_INFO, "setting position of output %s to %d, %d",
                output->wlr_output->name, config->x, config->y);
        layout = wlr_output_layout_add(server.output_layout, output->wlr_output,
                                       config->x, config->y);
    } else {
        layout = wlr_output_layout_add_auto(server.output_layout, output->wlr_output);
    }

    wlr_scene_output_layout_add_output(server.scene_layout, layout, output->scene_output);

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);
    output->usable_area = output_box;

    return output_box;
}

bool
output_initialize(struct wlr_output *wlr_output, struct output_config *config) {
    wlr_output_init_render(wlr_output, server.allocator, server.renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    if(config != NULL) {
        wlr_output_state_set_scale(&state, config->scale);
        /* we try to find the closest supported mode for this output, that means:
     *  - same resolution
     *  - closest refresh rate
     * if there is none we take the prefered mode for the output */
        struct wlr_output_mode *best_match = NULL;
        uint32_t best_match_diff = UINT32_MAX;

        struct wlr_output_mode *m;
        wl_list_for_each(m, &wlr_output->modes, link) {
            if(m->width == config->width && m->height == config->height
                && abs((int)m->refresh - (int)config->refresh_rate) < best_match_diff) {
                best_match = m;
                best_match_diff = abs((int)m->refresh - (int)config->refresh_rate);
            }
        }

        if(best_match != NULL) {
            wlr_log(WLR_INFO, "trying to set mode for output %s to %dx%d@%dmHz",
                    wlr_output->name, best_match->width, best_match->height, best_match->refresh);
            /* we set the mode and try to commit the state.
       * if it fails then we backup to the preffered. it should not fail! */
            wlr_output_state_set_mode(&state, best_match);
            bool success = wlr_output_commit_state(wlr_output, &state);
            if(!success) {
                success = output_apply_preffered_mode(wlr_output, &state);
                if(!success) {
                    wlr_log(WLR_ERROR, "couldn't apply the preffered mode to the output %s", wlr_output->name);
                    /* free the resource */
                    wlr_output_state_finish(&state);
                    return false;
                }
            }
        } else {
            bool success = output_apply_preffered_mode(wlr_output, &state);
            if(!success) {
                wlr_log(WLR_ERROR, "couldn't apply the preffered mode to the output %s", wlr_output->name);
                /* free the resource */
                wlr_output_state_finish(&state);
                return false;
            }
        }
    } else {
        wlr_log(WLR_INFO, "output %s not specified in the config; using the preffered mode.", wlr_output->name);
        /* if it is not specified in the config we take its preffered mode */
        bool success = output_apply_preffered_mode(wlr_output, &state);
        if(!success) {
            wlr_log(WLR_ERROR, "couldn't apply the preffered mode to the output %s", wlr_output->name);
            /* free the resource */
            wlr_output_state_finish(&state);
            return false;
        }
    }

    wlr_log(WLR_INFO, "successfully set up output %s", wlr_output->name);
    wlr_output_state_finish(&state);

    return true;
}

bool
output_apply_preffered_mode(struct wlr_output *wlr_output, struct wlr_output_state *state) {
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    wlr_output_state_set_mode(state, mode);

    return wlr_output_commit_state(wlr_output, state);
}

double
output_frame_duration_ms(struct mwc_output *output) {
    return 1000000.0 / output->wlr_output->refresh;
}

struct mwc_output *
output_get_relative(struct mwc_output *output, enum mwc_direction direction) {
    struct wlr_box original_output_box;
    wlr_output_layout_get_box(server.output_layout,
                              output->wlr_output, &original_output_box);

    original_output_box.width *= output->wlr_output->scale;
    original_output_box.height *= output->wlr_output->scale;

    uint32_t original_output_midpoint_x =
        original_output_box.x + original_output_box.width / 2;
    uint32_t original_output_midpoint_y =
        original_output_box.y + original_output_box.height / 2;

    struct mwc_output *o;
    wl_list_for_each(o, &server.outputs, link) {
        struct wlr_box output_box;
        wlr_output_layout_get_box(server.output_layout, o->wlr_output, &output_box);
        output_box.width *= o->wlr_output->scale;
        output_box.height *= o->wlr_output->scale;

        if(direction == MWC_LEFT &&
            original_output_box.x == output_box.x + output_box.width
            && original_output_midpoint_y > output_box.y
            && original_output_midpoint_y < output_box.y + output_box.height) {
            return o;
        } else if(direction == MWC_RIGHT
            && original_output_box.x + original_output_box.width == output_box.x
            && original_output_midpoint_y > output_box.y
            && original_output_midpoint_y < output_box.y + output_box.height) {
            return o;
        } else if(direction == MWC_UP
            && original_output_box.y == output_box.y + output_box.height
            && original_output_midpoint_x > output_box.x
            && original_output_midpoint_x < output_box.x + output_box.width) {
            return o;
        } else if(direction == MWC_DOWN
            && original_output_box.y + original_output_box.height == output_box.y
            && original_output_midpoint_x > output_box.x
            && original_output_midpoint_x < output_box.x + output_box.width) {
            return o;
        }
    }

    return NULL;
}

void
cursor_jump_output(struct mwc_output *output) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    wlr_cursor_warp(server.cursor, NULL,
                    output_box.x + output_box.width / 2.0,
                    output_box.y + output_box.height / 2.0);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    pointer_handle_focus(timespec_to_ms(&now), false);
}

void
focus_output(struct mwc_output *output, enum mwc_direction side) {
    assert(output != NULL);

    if(server.lock != NULL) {
        if(!wl_list_empty(&server.lock->surfaces)) {
            struct mwc_lock_surface *l = wl_container_of(server.lock->surfaces.next, l, link);
            focus_lock_surface(l);
        }
        return;
    }

    struct mwc_toplevel *focus_next = NULL;
    struct mwc_workspace *workspace = output->active_workspace;

    if(workspace->fullscreen_toplevel != NULL) {
        focus_next = workspace->fullscreen_toplevel;
    } else if(server.focused_toplevel == NULL || !server.focused_toplevel->floating) {
        bool master = server.focused_toplevel != NULL
            ? toplevel_is_master(server.focused_toplevel)
            : true;
        focus_next = layout_find_closest_toplevel(output->active_workspace,
                                                  master, side);
        /* if there are no tiled toplevels we try floating */
        if(focus_next == NULL) {
            focus_next = workspace_find_closest_floating_toplevel(output->active_workspace,
                                                                  side);
        }
    } else {
        focus_next = workspace_find_closest_floating_toplevel(output->active_workspace,
                                                              side);
        /* if there are no floating toplevels we try tiled */
        if(focus_next == NULL) {
            focus_next = layout_find_closest_toplevel(output->active_workspace,
                                                      true, side);
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

void
output_handle_frame(struct wl_listener *listener, void *data) {
    // this function is called every time an output is ready to display a frame,
    // generally at the output's refresh rate
    struct mwc_output *output = wl_container_of(listener, output, frame);

    output_draw(output);

    wlr_scene_output_commit(output->scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    wlr_scene_output_send_frame_done(output->scene_output, &now);
}

void
output_handle_request_state(struct wl_listener *listener, void *data) {
    /* this function is called when the backend requests a new state for
   * the output. for example, wayland and X11 backends request a new mode
   * when the output window is resized */
    struct mwc_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;

    wlr_output_commit_state(output->wlr_output, event->state);
}

void
output_handle_destroy(struct wl_listener *listener, void *data) {
    struct mwc_output *output = wl_container_of(listener, output, destroy);

    /* we want to transfer all the workspaces to a new output;
   * if this was the only output then idk what to do honestly, maybe have a temporary
   * stash thats going to hold them until some output is attached again? TODO */
    if(server.running) {
        struct wl_list *next = output->link.next;
        if(next == &server.outputs) {
            next = output->link.prev;
        }

        if(next != &server.outputs) {
            struct mwc_output *new = wl_container_of(next, new, link);
            bool valid_focus = server.focused_toplevel != NULL
                && server.focused_toplevel->workspace->output != output;
            if(!valid_focus) {
                focus_output(new, MWC_LEFT);
            }

            struct mwc_workspace *w, *tmp;
            wl_list_for_each_safe(w, tmp, &output->workspaces, link) {
                w->output = new;
                wl_list_remove(&w->link);
                wl_list_insert(&new->workspaces, &w->link);
                layout_set_pending_state(w);
            }
        }
    }

    if(output->session_lock_rect != NULL) {
        wlr_scene_node_destroy(&output->session_lock_rect->node);
    }

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);

    free(output);
}

struct wlr_box
output_create_centered_box(struct mwc_output *output, uint32_t width, uint32_t height) {
    return (struct wlr_box){
        .x = output->usable_area.x + (output->usable_area.width - width) / 2,
        .y = output->usable_area.y + (output->usable_area.height - height) / 2,
        .width = width,
        .height = height,
    };
}

