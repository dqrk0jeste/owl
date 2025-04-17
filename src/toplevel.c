#include "toplevel.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include "animations.h"
#include "config.h"
#include "helpers.h"
#include "ipc.h"
#include "layer_surface.h"
#include "layout.h"
#include "mwc.h"
#include "output.h"
#include "pointer.h"
#include "popup.h"
#include "rendering.h"
#include "text_node.h"
#include "view.h"
#include "workspace.h"

extern struct mwc_server server;

// todo: simplify the logic with ||
static bool
toplevel_matches_window_rule(struct mwc_toplevel *toplevel, struct window_rule_regex *condition) {
    char *app_id = toplevel->xdg_toplevel->app_id;
    char *title = toplevel->xdg_toplevel->title;

    bool matches_app_id;
    if(condition->has_app_id_regex) {
        if(app_id == NULL) {
            matches_app_id = false;
        } else {
            matches_app_id = regexec(&condition->app_id_regex, app_id, 0, NULL, 0) == 0;
        }
    } else {
        matches_app_id = true;
    }

    bool matches_title;
    if(condition->has_title_regex) {
        if(title == NULL) {
            matches_title = false;
        } else {
            matches_title = regexec(&condition->title_regex, title, 0, NULL, 0) == 0;
        }
    } else {
        matches_title = true;
    }

    return matches_app_id && matches_title;
}

static bool
toplevel_should_float(struct mwc_toplevel *toplevel) {
    // we make toplevels float if they have fixed size or are children of another
    // toplevel
    if((toplevel->xdg_toplevel->current.max_height &&
               toplevel->xdg_toplevel->current.max_height == toplevel->xdg_toplevel->current.min_height) ||
            (toplevel->xdg_toplevel->current.max_width &&
                    toplevel->xdg_toplevel->current.max_width == toplevel->xdg_toplevel->current.min_width) ||
            toplevel->xdg_toplevel->parent != NULL)
        return true;

    struct window_rule_float *w;
    wl_list_for_each(w, &server.config->window_rules.floating, link) {
        if(toplevel_matches_window_rule(toplevel, &w->condition)) {
            return true;
        }
    }

    return false;
}

static struct wlr_box
toplevel_floating_deco_box_for_own_size(struct mwc_toplevel *toplevel) {
    struct wlr_box geometry = toplevel_get_geometry(toplevel);

    // we need to convert this to deco box
    uint32_t width = geometry.width + 2 * server.config->border_width;
    uint32_t height = geometry.height + 2 * server.config->border_width;

    if(toplevel->has_titlebar) {
        height += server.config->titlebar_height;
    }

    return output_create_centered_box(toplevel->workspace->output, width, height);
}

// looks up window rules and returns true if found, with the size in `*width`
// and `*height`, else return false
bool
toplevel_get_floating_deco_size(struct mwc_toplevel *toplevel, uint32_t *width, uint32_t *height) {
    struct window_rule_size *w;
    wl_list_for_each(w, &server.config->window_rules.size, link) {
        if(toplevel_matches_window_rule(toplevel, &w->condition)) {
            if(w->relative_width) {
                *width = toplevel->workspace->output->usable_area.width * w->width / 100;
            } else {
                *width = w->width;
            }

            if(w->relative_height) {
                *height = toplevel->workspace->output->usable_area.height * w->height / 100;
            } else {
                *height = w->height;
            }

            return true;
        }
    }

    return false;
}

static void
toplevel_clip_tree(struct mwc_toplevel *toplevel, uint32_t width, uint32_t height) {
    struct wlr_box geometry = toplevel_get_geometry(toplevel);
    struct wlr_box clip_box = (struct wlr_box){
            .x = geometry.x,
            .y = geometry.y,
            .width = width,
            .height = height,
    };

    wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, &clip_box);

    // but we remove the clip from all the popups
    struct wlr_scene_node *n;
    wl_list_for_each(n, &toplevel->scene_tree->children, link) {
        struct mwc_view *view = n->data;
        if(view != NULL && view->type == MWC_POPUP) {
            wlr_scene_subsurface_tree_set_clip(n, NULL);
        }
    }
}

static void
toplevel_animation_callback(struct wlr_box current, bool done, void *user_data) {
    struct mwc_toplevel *toplevel = user_data;

    decoration_configure(toplevel->decoration, current.width, current.height);

    struct wlr_box content_box = decoration_get_content_box(toplevel->decoration, current);
    toplevel_clip_tree(toplevel, content_box.width, content_box.height);
    wlr_scene_node_set_position(&toplevel->scene_tree->node, content_box.x, content_box.y);

    if(done) {
        fx_transform_animation_destroy(toplevel->animation);
        toplevel->animation = NULL;
    }
}

static void
strip_decoration_of_size(uint32_t *width, uint32_t *height, bool has_border, bool has_titlebar) {
    uint32_t starting_width = *width;
    uint32_t starting_height = *height;
    if(has_border) {
        *width -= 2 * server.config->border_width;
        *height -= 2 * server.config->border_width;
    }

    if(has_titlebar) {
        *height -= server.config->titlebar_height;
    }

    // if there has been overflow we patch it to 1
    if(*width > starting_width) *width = 1;
    if(*height > starting_height) *height = 1;
}

static void
toplevel_handle_initial_commit(struct mwc_toplevel *toplevel) {
    // when an xdg_surface performs an initial commit, the compositor must
    // reply with a configure so the client can map the surface.

    // unlike other window rules we only check the floating ones on initial commit
    toplevel->floating = toplevel_should_float(toplevel);

    uint32_t width, height;
    if(toplevel->floating) {
        // we lookup window rules
        if(toplevel_get_floating_deco_size(toplevel, &width, &height)) {
            strip_decoration_of_size(&width, &height, toplevel->has_border, toplevel->has_titlebar);
        } else {
            width = height = 0;
            toplevel->should_choose_size = true;
        }
    } else {
        struct mwc_output *output = toplevel->workspace->output;

        uint32_t master_count = wl_list_length(&toplevel->workspace->masters);
        uint32_t slave_count = wl_list_length(&toplevel->workspace->slaves);
        if(master_count < server.config->master_count) {
            layout_get_masters_container_size(output, master_count + 1, slave_count, &width, &height);
        } else {
            layout_get_slaves_container_size(output, slave_count + 1, &width, &height);
        }
        strip_decoration_of_size(&width, &height, toplevel->has_border, toplevel->has_titlebar);
    }

    // send the initial configure
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);

    // we lie that its maximized so it behaves better
    wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
    wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, WLR_EDGE_TOP & WLR_EDGE_RIGHT & WLR_EDGE_BOTTOM & WLR_EDGE_LEFT);
}

static void
toplevel_handle_commit(struct wl_listener *listener, void *data) {
    // called when a new surface state is committed
    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

    if(!toplevel->xdg_toplevel->base->initialized) return;

    if(toplevel->xdg_toplevel->base->initial_commit) {
        toplevel_handle_initial_commit(toplevel);
        return;
    }

    if(!toplevel->xdg_toplevel->base->surface->mapped) return;

    // we only care about commits if we requested a size from them
    if(toplevel->should_choose_size) {
        toplevel->should_choose_size = false;
        struct wlr_box box = toplevel_floating_deco_box_for_own_size(toplevel);
        toplevel_set_state(toplevel, box);
    }

    // todo: try to handle the case when a floating toplevel changes its size on
    // its own, hit some roadblocks in my first attempt
}

uint32_t
toplevel_get_decoration_types(struct mwc_toplevel *toplevel) {
    uint32_t types = 0;
    if(toplevel->has_border) {
        types |= DECORATION_BORDER;
    }
    if(server.config->shadows) {
        types |= DECORATION_SHADOW;
    }
    if(toplevel->has_titlebar) {
        types |= DECORATION_TITLEBAR;
    }

    return types;
}

static void
toplevel_handle_map(struct wl_listener *listener, void *data) {
    // called when the surface is mapped, or ready to display on the screen
    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, map);

    // we keep the toplevel in this free field so we can obtain it when needed
    toplevel->xdg_toplevel->base->data = toplevel;

    // we insert it into a right list, and create a scene tree for the toplevel
    if(toplevel->floating) {
        wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);
        toplevel->scene_tree = wlr_scene_xdg_surface_create(server.floating_tree, toplevel->xdg_toplevel->base);
    } else {
        if(wl_list_length(&toplevel->workspace->masters) < server.config->master_count) {
            wl_list_insert(toplevel->workspace->masters.prev, &toplevel->link);
        } else {
            wl_list_insert(toplevel->workspace->slaves.prev, &toplevel->link);
        }

        toplevel->scene_tree = wlr_scene_xdg_surface_create(server.tiled_tree, toplevel->xdg_toplevel->base);
    }

    // in the node we want to keep information what that node represents. we do
    // that be keeping mwc_view in user data field, which is a union of all
    // possible 'things' we can have on the screen
    view_create_for_node(&toplevel->scene_tree->node, MWC_TOPLEVEL, toplevel);

    // create a decoration object
    toplevel->decoration = decoration_create(toplevel->scene_tree, toplevel_get_decoration_types(toplevel));

    // we set this flag for the popin animation
    toplevel->needs_popin_adjustment = server.config->animations;

    if(toplevel->floating) {
        // even if we have sent a concrete value here, we respect if the toplevel
        // chose another size it would be weird having a floating toplevel clipped
        // (thats exactly what happens when a toplevel changes its size on its own,
        // left to fix)
        struct wlr_box box = toplevel_floating_deco_box_for_own_size(toplevel);
        toplevel_set_state(toplevel, box);
    } else {
        layout_configure(toplevel->workspace);
    }

    focus_toplevel(toplevel);
}

// maybe clean this up a bit
static void
toplevel_handle_unmap(struct wl_listener *listener, void *data) {
    // called when the surface is unmapped, and should no longer be shown
    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

    struct mwc_workspace *workspace = toplevel->workspace;

    // if its the one focus should be returned to, remove it
    if(toplevel == server.prev_focused) {
        server.prev_focused = NULL;
    }

    // destroy the animation if its running
    if(toplevel->animation != NULL) {
        fx_transform_animation_destroy(toplevel->animation);
    }

    decoration_destroy(toplevel->decoration);

    // reset the cursor mode if the grabbed toplevel was unmapped
    if(toplevel == server.grabbed_toplevel) {
        cursor_stop_move_resize();

        // we find a toplevel to give focus to
        if(toplevel->floating && !wl_list_empty(&workspace->floating_toplevels)) {
            struct mwc_toplevel *t = wl_container_of(workspace->floating_toplevels.next, t, link);
            focus_toplevel(t);
        } else if(!wl_list_empty(&workspace->masters)) {
            struct mwc_toplevel *t = wl_container_of(workspace->masters.next, t, link);
            focus_toplevel(t);
        } else {
            server.focused_toplevel = NULL;
            ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
        }

        return;
    }

    // todo: extract this logic
    if(toplevel == workspace->fullscreen_toplevel) {
        workspace->fullscreen_toplevel = NULL;
        layers_under_fullscreen_set_enabled(workspace->output, true);

        struct mwc_toplevel *t;
        wl_list_for_each(t, &workspace->masters, link) {
            if(t == toplevel) continue;
            wlr_scene_node_set_enabled(&t->scene_tree->node, true);
        }
        wl_list_for_each(t, &workspace->slaves, link) {
            if(t == toplevel) continue;
            wlr_scene_node_set_enabled(&t->scene_tree->node, true);
        }
        wl_list_for_each(t, &workspace->floating_toplevels, link) {
            if(t == toplevel) continue;
            wlr_scene_node_set_enabled(&t->scene_tree->node, true);
        }
    }

    if(toplevel->floating) {
        if(server.focused_toplevel == toplevel) {
            // try to find other floating toplevels to give focus to
            struct wl_list *focus_next = toplevel->link.next;
            if(focus_next == &workspace->floating_toplevels) {
                focus_next = toplevel->link.prev;
                if(focus_next == &workspace->floating_toplevels) {
                    focus_next = workspace->masters.next;
                    if(focus_next == &workspace->masters) {
                        focus_next = NULL;
                    }
                }
            }

            if(focus_next != NULL) {
                struct mwc_toplevel *t = wl_container_of(focus_next, t, link);
                focus_toplevel(t);
            } else {
                server.focused_toplevel = NULL;
                ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
            }
        }

        wl_list_remove(&toplevel->link);
        return;
    }

    if(toplevel_is_master(toplevel)) {
        // we find a new master to replace him if possible
        if(!wl_list_empty(&workspace->slaves)) {
            struct mwc_toplevel *s = wl_container_of(workspace->slaves.prev, s, link);
            wl_list_remove(&s->link);
            wl_list_insert(workspace->masters.prev, &s->link);
        }
        if(toplevel == server.focused_toplevel) {
            // we want to give focus to some other toplevel
            struct wl_list *focus_next = toplevel->link.next;
            if(focus_next == &workspace->masters) {
                focus_next = toplevel->link.prev;
                if(focus_next == &workspace->masters) {
                    focus_next = workspace->floating_toplevels.next;
                    if(focus_next == &workspace->floating_toplevels) {
                        focus_next = NULL;
                    }
                }
            }

            if(focus_next != NULL) {
                struct mwc_toplevel *t = wl_container_of(focus_next, t, link);
                focus_toplevel(t);
            } else {
                server.focused_toplevel = NULL;
                ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
            }
        }

        // we finally remove him from the list
        wl_list_remove(&toplevel->link);
    } else {
        if(toplevel == server.focused_toplevel) {
            // we want to give focus to some other toplevel
            struct wl_list *focus_next = toplevel->link.next;
            if(focus_next == &workspace->slaves) {
                focus_next = toplevel->link.prev;
                if(focus_next == &workspace->slaves) {
                    // take the last master
                    focus_next = workspace->masters.prev;
                }
            }
            // here its not possible to have no other toplevel to give focus,
            // there are always master_count masters available
            struct mwc_toplevel *t = wl_container_of(focus_next, t, link);
            focus_toplevel(t);
        }

        wl_list_remove(&toplevel->link);
    }

    layout_configure(toplevel->workspace);
}

static void
toplevel_handle_destroy(struct wl_listener *listener, void *data) {
    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

    wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign_toplevel_handle);

    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->commit.link);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);

    free(toplevel);
}

void
toplevel_handle_request_move(struct wl_listener *listener, void *data) {
    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);

    struct mwc_view *view = pointer_get_view_under_cursor();
    if(view == NULL) return;

    struct mwc_toplevel *focused = view_try_get_toplevel(view);
    if(toplevel != focused) return;

    toplevel_start_move(toplevel, true);
}

void
toplevel_handle_request_resize(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_resize_event *event = data;

    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);

    struct mwc_view *view = pointer_get_view_under_cursor();
    if(view == NULL) return;

    struct mwc_toplevel *focused = view_try_get_toplevel(view);
    if(toplevel != focused) return;

    toplevel_start_resize(toplevel, event->edges, true);
}

void
toplevel_handle_request_maximize(struct wl_listener *listener, void *data) {
    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
    if(toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    }
}

void
toplevel_handle_request_fullscreen(struct wl_listener *listener, void *data) {
    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);

    if(toplevel->xdg_toplevel->requested.fullscreen) {
        toplevel_set_fullscreen(toplevel);
    } else {
        toplevel_unset_fullscreen(toplevel);
    }
}

static void
recheck_opacity_rules(struct mwc_toplevel *toplevel) {
    struct window_rule_opacity *iter;
    wl_list_for_each(iter, &server.config->window_rules.opacity, link) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->active_opacity = iter->active_value;
            toplevel->inactive_opacity = iter->inactive_value;
            return;
        }
    }

    toplevel->active_opacity = server.config->opacity.active;
    toplevel->inactive_opacity = server.config->opacity.inactive;
}

static void
recheck_no_titlebar_rules(struct mwc_toplevel *toplevel) {
    // we only care about these rules if we are drawing the titlebars globally
    if(!server.config->titlebars) {
        toplevel->has_titlebar = false;
        return;
    }

    struct window_rule_no_titlebar *iter;
    wl_list_for_each(iter, &server.config->window_rules.no_titlebar, link) {
        if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
            toplevel->has_titlebar = false;
            return;
        }
    }

    toplevel->has_titlebar = true;
}

static void
recheck_no_border_rules(struct mwc_toplevel *toplevel) {
    // we only care about these rules if we are drawing the borders globally
    if(!server.config->borders) {
        toplevel->has_border = false;
        return;
    }

    toplevel->has_border = true;

    // struct window_rule_no_titlebar *iter;
    // wl_list_for_each(iter, &server.config->window_rules.no_titlebar, link) {
    //     if(toplevel_matches_window_rule(toplevel, &iter->condition)) {
    //         toplevel->has_titlebar = false;
    //         return;
    //     }
    // }
    //
    // toplevel->has_titlebar = true;
}

void
toplevel_recheck_window_rules(struct mwc_toplevel *toplevel) {
    recheck_opacity_rules(toplevel);
    recheck_no_titlebar_rules(toplevel);
    recheck_no_border_rules(toplevel);
}

void
toplevel_handle_set_app_id(struct wl_listener *listener, void *data) {
    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, set_app_id);

    toplevel_recheck_window_rules(toplevel);

    if(toplevel->decoration != NULL) {
        decoration_set_types(toplevel->decoration, toplevel_get_decoration_types(toplevel));
    }

    wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_toplevel_handle, toplevel->xdg_toplevel->app_id);

    if(toplevel == server.focused_toplevel) {
        ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
    }
}

void
toplevel_handle_set_title(struct wl_listener *listener, void *data) {
    struct mwc_toplevel *toplevel = wl_container_of(listener, toplevel, set_title);

    toplevel_recheck_window_rules(toplevel);
    if(toplevel->decoration != NULL) {
        decoration_set_types(toplevel->decoration, toplevel_get_decoration_types(toplevel));
    }

    wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_toplevel_handle, toplevel->xdg_toplevel->title);

    // todo: extract this into a function in decoration.c
    if(toplevel->decoration != NULL && toplevel->decoration->titlebar.title != NULL) {
        text_node_set_text(toplevel->decoration->titlebar.title, toplevel->xdg_toplevel->title);
    }

    if(toplevel == server.focused_toplevel) {
        ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
    }
}

void
cursor_jump_focused_toplevel(void) {
    struct mwc_toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL) return;

    // todo: investigate this
    wlr_cursor_warp(server.cursor, NULL, toplevel->scene_tree->node.x + toplevel->deco_box.width / 2.0,
            toplevel->scene_tree->node.y + toplevel->deco_box.height / 2.0);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    pointer_handle_focus(timespec_to_ms(&now), false);
}

void
toplevel_set_fullscreen(struct mwc_toplevel *toplevel) {
    if(!toplevel->xdg_toplevel->base->surface->mapped) return;

    if(toplevel->workspace->fullscreen_toplevel != NULL) return;
    if(toplevel == server.grabbed_toplevel) return;

    struct mwc_workspace *workspace = toplevel->workspace;
    struct mwc_output *output = workspace->output;

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

    toplevel->prev_deco_box = toplevel->deco_box;

    workspace->fullscreen_toplevel = toplevel;
    toplevel->fullscreen = true;

    wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign_toplevel_handle, true);

    // disable the decorations; be sure to call this before set state so it gets the right size
    decoration_set_enabled(toplevel->decoration, false);

    toplevel_set_state(toplevel, output_box);
    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.fullscreen_tree);

    // we disable all the other toplevels so they are not seen if there is transparency
    workspace_toplevels_set_enabled(workspace, false);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
    // we also disable bottom and top layer surfaces, and leave only the backgorund needed for blur
    layers_under_fullscreen_set_enabled(workspace->output, false);
}

void
toplevel_unset_fullscreen(struct mwc_toplevel *toplevel) {
    if(toplevel->workspace->fullscreen_toplevel != toplevel) return;

    struct mwc_workspace *workspace = toplevel->workspace;

    workspace->fullscreen_toplevel = NULL;
    toplevel->fullscreen = false;

    wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign_toplevel_handle, false);

    // enable the decorations; be sure to call this before set state so it gets the right size
    decoration_set_enabled(toplevel->decoration, true);

    if(toplevel->floating) {
        toplevel_set_state(toplevel, toplevel->prev_deco_box);
        wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
    } else {
        wlr_scene_node_reparent(&toplevel->scene_tree->node, server.tiled_tree);
    }

    // reenable the toplevels and layers
    workspace_toplevels_set_enabled(workspace, true);
    layers_under_fullscreen_set_enabled(workspace->output, true);

    layout_configure(workspace);
}

void
unfocus_focused_toplevel(void) {
    struct mwc_toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL) return;

    server.focused_toplevel = NULL;

    // deactivate the surface
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);

    // clear all focus on the keyboard, focusing new should set new toplevel focus
    wlr_seat_keyboard_clear_focus(server.seat);
    wlr_seat_pointer_clear_focus(server.seat);

    ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
    wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign_toplevel_handle, false);

    decoration_set_active(toplevel->decoration, false);
}

void
focus_toplevel(struct mwc_toplevel *toplevel) {
    if(server.lock != NULL || server.exclusive || server.grabbed_toplevel != NULL ||
            (toplevel->workspace->fullscreen_toplevel != NULL && toplevel != toplevel->workspace->fullscreen_toplevel))
        return;

    struct mwc_toplevel *prev_toplevel = server.focused_toplevel;
    if(prev_toplevel == toplevel) return;

    // we change the workspace if needed, this is primarly because of the
    // activation protocol
    change_workspace(toplevel->workspace, true);

    if(prev_toplevel != NULL) {
        wlr_xdg_toplevel_set_activated(prev_toplevel->xdg_toplevel, false);
        wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign_toplevel_handle, false);

        decoration_set_active(prev_toplevel->decoration, false);
    }

    server.focused_toplevel = toplevel;

    if(toplevel->floating) {
        wl_list_remove(&toplevel->link);
        wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);
    }

    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

    struct wlr_seat *seat = server.seat;
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(seat, toplevel->xdg_toplevel->base->surface, keyboard->keycodes,
                keyboard->num_keycodes, &keyboard->modifiers);
    }

    ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
    wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign_toplevel_handle, true);

    decoration_set_active(toplevel->decoration, true);
}

struct mwc_toplevel *
toplevel_find_closest_floating_on_workspace(struct mwc_toplevel *toplevel, enum mwc_direction direction) {
    assert(toplevel->floating);
    struct mwc_workspace *workspace = toplevel->workspace;

    struct mwc_toplevel *min = NULL;
    uint32_t min_val = UINT32_MAX;

    struct mwc_toplevel *t;
    switch(direction) {
        case MWC_UP: {
            wl_list_for_each(t, &workspace->floating_toplevels, link) {
                if(t == toplevel || Y(t) > Y(toplevel)) continue;

                uint32_t dy = abs((int)Y(toplevel) - Y(t));
                if(dy < min_val) {
                    min = t;
                    min_val = dy;
                }
            }
            return min;
        }
        case MWC_DOWN: {
            wl_list_for_each(t, &workspace->floating_toplevels, link) {
                if(t == toplevel || Y(t) < Y(toplevel)) continue;

                uint32_t dy = abs((int)Y(toplevel) - Y(t));
                if(dy < min_val) {
                    min = t;
                    min_val = dy;
                }
            }
            return min;
        }
        case MWC_LEFT: {
            wl_list_for_each(t, &workspace->floating_toplevels, link) {
                if(t == toplevel || X(t) > X(toplevel)) continue;

                uint32_t dx = abs((int)X(toplevel) - X(t));
                if(dx < min_val) {
                    min = t;
                    min_val = dx;
                }
            }
            return min;
        }
        case MWC_RIGHT: {
            wl_list_for_each(t, &workspace->floating_toplevels, link) {
                if(t == toplevel || X(t) < X(toplevel)) continue;

                uint32_t dx = abs((int)X(toplevel) - X(t));
                if(dx < min_val) {
                    min = t;
                    min_val = dx;
                }
            }
            return min;
        }
    }
}

struct mwc_output *
toplevel_get_primary_output(struct mwc_toplevel *toplevel) {
    struct wlr_box intersection_box;
    struct wlr_box output_box;
    uint32_t max_area = 0;
    struct mwc_output *max_area_output = NULL;

    struct mwc_output *o;
    wl_list_for_each(o, &server.outputs, link) {
        wlr_output_layout_get_box(server.output_layout, o->wlr_output, &output_box);
        bool intersects = wlr_box_intersection(&intersection_box, &toplevel->deco_box, &output_box);
        if(intersects && box_area(&intersection_box) > max_area) {
            max_area = box_area(&intersection_box);
            max_area_output = o;
        }
    }

    return max_area_output;
}

uint32_t
toplevel_get_closest_corner(struct wlr_cursor *cursor, struct mwc_toplevel *toplevel) {
    struct wlr_box current = toplevel_get_current_display_deco_box(toplevel);

    int32_t left_dist = cursor->x - current.x;
    int32_t right_dist = current.width - left_dist;
    int32_t top_dist = cursor->y - current.y;
    int32_t bottom_dist = current.height - top_dist;

    uint32_t edges = 0;
    if(left_dist <= right_dist) {
        edges |= WLR_EDGE_LEFT;
    } else {
        edges |= WLR_EDGE_RIGHT;
    }

    if(top_dist <= bottom_dist) {
        edges |= WLR_EDGE_TOP;
    } else {
        edges |= WLR_EDGE_BOTTOM;
    }

    return edges;
}

void
xdg_activation_handle_token_destroy(struct wl_listener *listener, void *data) {
    struct mwc_token *token_data = wl_container_of(listener, token_data, destroy);
    wl_list_remove(&token_data->destroy.link);

    free(token_data);
}

void
xdg_activation_handle_new_token(struct wl_listener *listener, void *data) {
    struct wlr_xdg_activation_token_v1 *wlr_token = data;
    if(wlr_token->surface == NULL || wlr_token->seat == NULL) return;

    struct mwc_token *token = calloc(1, sizeof(*token));
    token->wlr_token = wlr_token;
    wlr_token->data = token;

    token->destroy.notify = xdg_activation_handle_token_destroy;
    wl_signal_add(&wlr_token->events.destroy, &token->destroy);
}

void
xdg_activation_handle_request(struct wl_listener *listener, void *data) {
    const struct wlr_xdg_activation_v1_request_activate_event *event = data;

    struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(event->surface);
    if(xdg_surface == NULL) return;

    struct wlr_scene_tree *tree = xdg_surface->data;
    // this happens if the toplevel has not been mapped yet. anyway it does not
    // make sense to request that i activate this surface that is not on the
    // screen
    if(tree == NULL) return;

    struct mwc_view *view = tree->node.data;
    if(view == NULL) return;

    if(view->type == MWC_POPUP) {
        view = popup_get_root_parent(view->popup);
    }

    if(view->type != MWC_TOPLEVEL) return;

    struct mwc_toplevel *toplevel = view->toplevel;
    focus_toplevel(toplevel);
}

struct wlr_box
toplevel_get_current_display_deco_box(struct mwc_toplevel *toplevel) {
    if(toplevel->animation != NULL) {
        return fx_transform_animation_get_current(toplevel->animation);
    }

    return toplevel->deco_box;
}

struct wlr_box
toplevel_get_current_display_content_box(struct mwc_toplevel *toplevel) {
    struct wlr_box deco_box = toplevel_get_current_display_deco_box(toplevel);
    return decoration_get_content_box(toplevel->decoration, deco_box);
}

// todo: maybe dont animate thing that are not shown on the screen by checking
// node.enabled?
void
toplevel_set_state(struct mwc_toplevel *toplevel, struct wlr_box deco_box) {
    struct wlr_box content_box = decoration_get_content_box(toplevel->decoration, deco_box);

    // this may have been left at true if the user was fast enough
    toplevel->should_choose_size = false;

    // todo: maybe find a more flexible solution to not send this when moving a toplevel, but oh well, its not that big
    // of a deal send a configure to the client;
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, content_box.width, content_box.height);

    // we find where the toplevel is currently, this is just a toplevel box, with no decorations
    struct wlr_box current;
    if(toplevel->needs_popin_adjustment) {
        // we patch the animation for the popin effect
        current = (struct wlr_box){
                .x = deco_box.x + (deco_box.width - server.config->toplevel_minimum_width) / 2,
                .y = deco_box.y + (deco_box.height - server.config->toplevel_minimum_height) / 2,
                .width = server.config->toplevel_minimum_width,
                .height = server.config->toplevel_minimum_height,
        };
        toplevel->needs_popin_adjustment = false;
    } else if(toplevel->animation != NULL) {
        current = fx_transform_animation_get_current(toplevel->animation);
        fx_transform_animation_destroy(toplevel->animation);
        toplevel->animation = NULL;
    } else {
        current = toplevel->deco_box;
    }

    if(server.config->animations && toplevel != server.grabbed_toplevel) {
        toplevel->animation = fx_transform_animation_create(current, deco_box, server.config->animation_duration,
                server.config->animation_curve, toplevel_animation_callback, toplevel);

    } else {
        decoration_configure(toplevel->decoration, deco_box.width, deco_box.height);
        toplevel_clip_tree(toplevel, content_box.width, content_box.height);
        wlr_scene_node_set_position(&toplevel->scene_tree->node, content_box.x, content_box.y);
    }

    toplevel->content_box = content_box;
    toplevel->deco_box = deco_box;
}

void
toplevel_floating_set_own_size(struct mwc_toplevel *toplevel) {
    assert(toplevel->floating);

    toplevel->should_choose_size = true;
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
}

struct wlr_box
toplevel_get_geometry(struct mwc_toplevel *toplevel) {
    struct wlr_box geometry;
    wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);

    return geometry;
}

void
toplevel_start_move(struct mwc_toplevel *toplevel, bool client_driven) {
    if(server.grabbed_toplevel != NULL) return;

    server.grabbed_toplevel = toplevel;
    server.cursor_mode = MWC_CURSOR_MOVE;
    server.client_driven_move_resize = client_driven;

    server.grab_x = server.cursor->x;
    server.grab_y = server.cursor->y;

    server.grabbed_toplevel_initial_box = toplevel_get_current_display_deco_box(toplevel);

    if(toplevel->animation != NULL) {
        // if there is an animation running we need to stop it and start the drag
        // from there we do that be first stopping the animation, and taking the
        // current state of the toplevel as the initial toplevel box
        fx_transform_animation_destroy(toplevel->animation);
        toplevel->animation = NULL;

        // we also immediatelly set a new toplevel state because the toplevel may
        // have been streched or clipped, so it would look weird
        toplevel_set_state(toplevel, server.grabbed_toplevel_initial_box);
    }

    if(toplevel->floating) {
        wl_list_remove(&toplevel->link);
    } else {
        bool is_master = toplevel_is_master(toplevel);
        wl_list_remove(&toplevel->link);
        if(is_master && !wl_list_empty(&toplevel->workspace->slaves)) {
            struct mwc_toplevel *last = wl_container_of(toplevel->workspace->slaves.prev, last, link);
            wl_list_remove(&last->link);
            wl_list_insert(toplevel->workspace->masters.prev, &last->link);
        }

        layout_configure(toplevel->workspace);
    }
}

void
toplevel_start_resize(struct mwc_toplevel *toplevel, uint32_t edges, bool client_driven) {
    if(server.grabbed_toplevel != NULL) return;

    server.grabbed_toplevel = toplevel;
    server.cursor_mode = MWC_CURSOR_RESIZE;
    server.client_driven_move_resize = client_driven;

    server.grab_x = server.cursor->x;
    server.grab_y = server.cursor->y;

    server.resize_edges = edges;
    server.grabbed_toplevel_initial_box = toplevel_get_current_display_deco_box(toplevel);

    if(toplevel->animation != NULL) {
        // if there is an animation running we need to stop it and start the drag
        // from there we do that be first stopping the animation, and taking the
        // current state of the toplevel as the initial toplevel box
        fx_transform_animation_destroy(toplevel->animation);
        toplevel->animation = NULL;

        // we also immediatelly set a new toplevel state because the toplevel may
        // have been streched or clipped, so it would look weird
        toplevel_set_state(toplevel, server.grabbed_toplevel_initial_box);
    }
}

void
server_handle_new_toplevel(struct wl_listener *listener, void *data) {
    // this event is raised when a client creates a new toplevel
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    // allocate an mwc_toplevel for this surface
    struct mwc_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    toplevel->xdg_toplevel = xdg_toplevel;

    // these values can be later rewritten by window rules; check toplevel_recheck_window_rules()
    toplevel->active_opacity = server.config->opacity.active;
    toplevel->inactive_opacity = server.config->opacity.inactive;
    toplevel->has_titlebar = server.config->titlebars;
    toplevel->has_border = server.config->borders;

    // we give it the currently active workspace
    toplevel->workspace = server.active_workspace;

    wlr_fractional_scale_v1_notify_scale(toplevel->xdg_toplevel->base->surface,
            toplevel->workspace->output->wlr_output->scale);
    wlr_surface_set_preferred_buffer_scale(toplevel->xdg_toplevel->base->surface,
            ceil(toplevel->workspace->output->wlr_output->scale));

    // add foreign toplevel handler
    toplevel->foreign_toplevel_handle = wlr_foreign_toplevel_handle_v1_create(server.foreign_toplevel_manager);

    toplevel->map.notify = toplevel_handle_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);

    toplevel->unmap.notify = toplevel_handle_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);

    toplevel->commit.notify = toplevel_handle_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

    toplevel->destroy.notify = toplevel_handle_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

    toplevel->request_move.notify = toplevel_handle_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);

    toplevel->request_resize.notify = toplevel_handle_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);

    toplevel->request_maximize.notify = toplevel_handle_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);

    toplevel->request_fullscreen.notify = toplevel_handle_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);

    toplevel->set_app_id.notify = toplevel_handle_set_app_id;
    wl_signal_add(&xdg_toplevel->events.set_app_id, &toplevel->set_app_id);

    toplevel->set_title.notify = toplevel_handle_set_title;
    wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
}
