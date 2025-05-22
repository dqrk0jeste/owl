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
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include "animations.h"
#include "array.h"
#include "config.h"
#include "cursor.h"
#include "helpers.h"
#include "ipc.h"
#include "layer_shell.h"
#include "layout.h"
#include "mwc.h"
#include "output.h"
#include "rules.h"
#include "text_node.h"
#include "view.h"
#include "workspace.h"

extern struct server server;

bool
toplevel_is_tiled(struct toplevel *toplevel) {
    return toplevel->mode == TOPLEVEL_MODE_MASTER || toplevel->mode == TOPLEVEL_MODE_SLAVE;
}

static void
toplevel_handle_own_size(struct toplevel *toplevel) {
    // remove the flag
    toplevel->should_choose_size = false;

    struct wlr_box geometry = toplevel_get_geometry(toplevel);

    int width = geometry.width, height = geometry.height;
    decoration_get_decoration_size(&toplevel->decoration, &width, &height);

    toplevel_set_state(toplevel, output_create_centered_box(toplevel->workspace->output, width, height));
}

static void
toplevel_clip_tree(struct toplevel *toplevel, int width, int height) {
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
        struct view *view = n->data;
        if(view != NULL && view->type == VIEW_POPUP) {
            wlr_scene_subsurface_tree_set_clip(n, NULL);
        }
    }
}

static void
handle_initial_commit(struct toplevel *toplevel) {
    rules_update_for_toplevel(toplevel);

    // unlike other toplevel rules we only check the floating ones on initial commit
    int width, height;
    if(rules_get_toplevel_default_mode(toplevel) == TOPLEVEL_DEFAULT_MODE_FLOATING) {
        toplevel->mode = TOPLEVEL_MODE_FLOATING;
        if(toplevel->default_width != 0 && toplevel->default_height != 0) {
            width = toplevel->default_width, height = toplevel->default_height;
            if(toplevel->width_is_relative)
                width *= toplevel->workspace->output->usable_area.width / 100.0;
            if(toplevel->height_is_relative)
                height *= toplevel->workspace->output->usable_area.height / 100.0;

            decoration_get_content_size(&toplevel->decoration, &width, &height);
        } else {
            width = height = 0;
            toplevel->should_choose_size = true;
        }
    } else {
        // mode will be set on map, since this function can only guess what the state will be when the toplevel maps. if
        // there are multiple toplevels in the process of getting their toplevels on the screen, this might be off
        if(toplevel->workspace->master_count < toplevel->workspace->output->master_count) {
            layout_get_masters_container_size(toplevel->workspace, toplevel->workspace->master_count + 1,
                    toplevel->workspace->slave_count, &width, &height);
        } else {
            layout_get_slaves_container_size(toplevel->workspace, toplevel->workspace->master_count,
                    toplevel->workspace->slave_count + 1, &width, &height);
        }

        decoration_get_content_size(&toplevel->decoration, &width, &height);
    }

    // send the initial configure
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);

    wlr_xdg_toplevel_set_wm_capabilities(toplevel->xdg_toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
    // we lie that its maximized so it behaves better
    wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
    wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, WLR_EDGE_TOP & WLR_EDGE_RIGHT & WLR_EDGE_BOTTOM & WLR_EDGE_LEFT);
}

static void
handle_commit(struct wl_listener *listener, void *data) {
    // called when a new surface state is committed
    struct toplevel *toplevel = wl_container_of(listener, toplevel, commit);

    if(toplevel->xdg_toplevel->base->initial_commit) {
        handle_initial_commit(toplevel);
        return;
    }

    if(!toplevel->xdg_toplevel->base->surface->mapped)
        return;

    // we only care about commits if we requested a size from them; todo: try to handle the case when a floating
    // toplevel changes its size on its own
    if(toplevel->should_choose_size) {
        toplevel_handle_own_size(toplevel);
        return;
    }

    // toplevels geometry might have changed, so we update the clip accordingly; this can happen when the user toggles
    // `client_side_decorations` option in the configuration and the client starts drawing them
    int width, height;
    toplevel_get_current_display_content_size(toplevel, &width, &height);
    toplevel_clip_tree(toplevel, width, height);
}

static void
handle_map(struct wl_listener *listener, void *data) {
    // called when the surface is mapped, or ready to display on the screen
    struct toplevel *toplevel = wl_container_of(listener, toplevel, map);

    // enable the node
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
    // set initial decoration blur
    decoration_set_blur(&toplevel->decoration, toplevel->blur);

    // we set this flag for the pop-in animation
    toplevel->needs_popin_adjustment = server.config->animations.enabled;

    // insert it into a right list, and reparent to a floating tree if needed
    if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        wl_list_insert(&toplevel->workspace->floating, &toplevel->link);
        wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
        // even if we have sent a concrete value here, we respect if the toplevel chose another size; it would be weird
        // having a floating toplevel clipped (thats exactly what happens when a toplevel changes its size on its own,
        // and is left to fix)
        toplevel_handle_own_size(toplevel);
    } else {
        layout_add(toplevel->workspace, toplevel);
        layout_configure(toplevel->workspace);
    }

    focus_toplevel(toplevel, false);
}

static void
handle_unmap(struct wl_listener *listener, void *data) {
    // called when the surface is unmapped, and should no longer be shown
    struct toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

    struct workspace *workspace = toplevel->workspace;

    // if its the one focus should be returned to, remove it
    if(toplevel == server.prev_focused) {
        server.prev_focused = NULL;
    }

    // destroy the animation if its running
    if(toplevel->animation != NULL) {
        fx_transform_animation_destroy(toplevel->animation);
    }

    // reset the mode if the grabbed toplevel was unmapped
    if(toplevel == server.grabbed_toplevel) {
        server.grabbed_toplevel = NULL;
        server.mode = SERVER_MODE_NORMAL;

        if(toplevel == server.focused_toplevel) {
            // note: we use cursor position here since `toplevel->workspace` isnt up to date
            server.focused_toplevel = NULL;
            if(server.active_workspace->fullscreen != NULL) {
                focus_toplevel(server.active_workspace->fullscreen, false);
            } else if(has_floating(server.active_workspace)) {
                focus_toplevel(first_floating(server.active_workspace), false);
            } else if(has_masters(server.active_workspace)) {
                focus_toplevel(first_master(server.active_workspace), false);
            } else {
                ipc_send_focused_toplevel();
            }
        }
    } else if(toplevel->mode == TOPLEVEL_MODE_FULLSCREEN) {
        workspace->fullscreen = NULL;
        layers_under_fullscreen_set_enabled(workspace->output, true);
        workspace_toplevels_set_enabled(workspace, true);

        if(toplevel == server.focused_toplevel) {
            server.focused_toplevel = NULL;
            if(has_floating(workspace)) {
                focus_toplevel(first_floating(workspace), false);
            } else if(has_masters(workspace)) {
                focus_toplevel(first_master(workspace), false);
            } else {
                ipc_send_focused_toplevel();
            }
        }
    } else if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        if(toplevel == server.focused_toplevel) {
            server.focused_toplevel = NULL;

            struct toplevel *focus;
            if((focus = next_floating(toplevel)) != NULL) {
                focus_toplevel(focus, false);
            } else if((focus = prev_floating(toplevel)) != NULL) {
                focus_toplevel(focus, false);
            } else if((focus = first_master(workspace)) != NULL) {
                focus_toplevel(focus, false);
            } else {
                ipc_send_focused_toplevel();
            }
        }

        wl_list_remove(&toplevel->link);
    } else if(toplevel->mode == TOPLEVEL_MODE_MASTER) {
        toplevel->workspace->master_count--;
        // find a slave to replace this master
        if(has_slaves(workspace)) {
            promote_last_slave(workspace);
        }

        if(toplevel == server.focused_toplevel) {
            server.focused_toplevel = NULL;

            struct toplevel *focus;
            if((focus = first_floating(workspace)) != NULL) {
                focus_toplevel(focus, false);
            } else if((focus = next_master(toplevel)) != NULL) {
                focus_toplevel(focus, false);
            } else if((focus = prev_master(toplevel)) != NULL) {
                focus_toplevel(focus, false);
            } else {
                ipc_send_focused_toplevel();
            }
        }

        wl_list_remove(&toplevel->link);
        layout_configure(workspace);
    } else {
        toplevel->workspace->slave_count--;

        if(toplevel == server.focused_toplevel) {
            server.focused_toplevel = NULL;

            struct toplevel *focus;
            if((focus = first_floating(workspace)) != NULL) {
                focus_toplevel(focus, false);
            } else if((focus = next_slave(toplevel)) != NULL) {
                focus_toplevel(focus, false);
            } else if((focus = prev_slave(toplevel)) != NULL) {
                focus_toplevel(focus, false);
            } else {
                focus_toplevel(last_master(workspace), false);
            }
        }

        wl_list_remove(&toplevel->link);
        layout_configure(workspace);
    }
}

static void
handle_request_move(struct wl_listener *listener, void *data) {
    if(server.mode != SERVER_MODE_NORMAL)
        return;

    struct toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    if(toplevel->mode == TOPLEVEL_MODE_FULLSCREEN)
        return;

    // we make sure that the toplevel has the pointer focus
    struct toplevel *pointer_toplevel = cursor_get_toplevel();
    if(toplevel != pointer_toplevel)
        return;

    toplevel_start_move(toplevel, false);
}

static void
handle_request_resize(struct wl_listener *listener, void *data) {
    if(server.mode != SERVER_MODE_NORMAL)
        return;

    struct toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    if(toplevel->mode != TOPLEVEL_MODE_FLOATING)
        return;

    // we make sure that the toplevel has pointer focus
    struct toplevel *pointer_toplevel = cursor_get_toplevel();
    if(toplevel != pointer_toplevel)
        return;

    struct wlr_xdg_toplevel_resize_event *event = data;
    toplevel_start_resize(toplevel, event->edges, true);
}

static void
handle_request_maximize(struct wl_listener *listener, void *data) {
    struct toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);

    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

static void
handle_request_fullscreen(struct wl_listener *listener, void *data) {
    struct toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);

    if(!toplevel->xdg_toplevel->base->surface->mapped) {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
        return;
    }

    if(toplevel->xdg_toplevel->requested.fullscreen) {
        toplevel_set_fullscreen(toplevel);
    } else {
        toplevel_unset_fullscreen(toplevel);
    }
}

static void
handle_set_app_id(struct wl_listener *listener, void *data) {
    struct toplevel *toplevel = wl_container_of(listener, toplevel, set_app_id);

    rules_update_for_toplevel(toplevel);

    wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_toplevel_handle->wlr_handle,
            toplevel->xdg_toplevel->app_id);

    if(toplevel == server.focused_toplevel) {
        ipc_send_focused_toplevel();
    }
}

static void
handle_set_title(struct wl_listener *listener, void *data) {
    struct toplevel *toplevel = wl_container_of(listener, toplevel, set_title);

    rules_update_for_toplevel(toplevel);
    decoration_set_title(&toplevel->decoration, toplevel->xdg_toplevel->title);

    wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_toplevel_handle->wlr_handle,
            toplevel->xdg_toplevel->title);

    if(toplevel == server.focused_toplevel) {
        ipc_send_focused_toplevel();
    }
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

    foreign_toplevel_handle_destroy(toplevel->foreign_toplevel_handle);

    decoration_destroy(&toplevel->decoration);
    wlr_scene_node_destroy(&toplevel->scene_tree->node);

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

static void
toplevel_raise_children_above(struct toplevel *toplevel) {
    struct toplevel *iter;
    wl_list_for_each(iter, &toplevel->workspace->floating, link) {
        if(iter->xdg_toplevel->parent == toplevel->xdg_toplevel) {
            // if its a child of this toplevel we raise it above this one, which will recursively raise all of its
            // children above itself
            wlr_scene_node_place_above(&iter->scene_tree->node, &toplevel->scene_tree->node);
            toplevel_raise_children_above(iter);
        }
    }
}

static void
toplevel_raise_parent_just_bellow(struct toplevel *toplevel) {
    if(toplevel->xdg_toplevel->parent == NULL)
        return;

    struct toplevel *parent = toplevel->xdg_toplevel->parent->base->data;
    if(parent->mode == TOPLEVEL_MODE_FLOATING) {
        // we raise this one and its parent (if any)
        wlr_scene_node_place_below(&parent->scene_tree->node, &toplevel->scene_tree->node);
        toplevel_raise_parent_just_bellow(parent);
    }
}

void
toplevel_raise_to_top(struct toplevel *toplevel) {
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

    if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        // if floating we raise its parent (and parents parent and so on)
        toplevel_raise_parent_just_bellow(toplevel);
        // and also raise its children above this one
        toplevel_raise_children_above(toplevel);
    }
}

void
toplevel_set_fullscreen(struct toplevel *toplevel) {
    if(toplevel->workspace->fullscreen != NULL || toplevel == server.grabbed_toplevel)
        return;

    struct workspace *workspace = toplevel->workspace;

    toplevel->prev_mode = toplevel->mode;
    if(toplevel->prev_mode == TOPLEVEL_MODE_FLOATING) {
        toplevel->prev_deco_box = toplevel->deco_box;
    } else if(toplevel->prev_mode == TOPLEVEL_MODE_MASTER) {
        toplevel->prev_index = list_index_of(&workspace->masters, &toplevel->link);
        workspace->master_count--;
        // replace this one with a slave
        if(has_slaves(workspace)) {
            promote_last_slave(workspace);
        }
    } else {
        workspace->slave_count--;
        toplevel->prev_index = list_index_of(&workspace->slaves, &toplevel->link);
    }

    wl_list_remove(&toplevel->link);
    toplevel->mode = TOPLEVEL_MODE_FULLSCREEN;
    workspace->fullscreen = toplevel;

    wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign_toplevel_handle->wlr_handle, true);

    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.fullscreen_tree);

    // before we set the state for this toplevel be sure to recheck the rules
    rules_update_for_toplevel(toplevel);

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, workspace->output->wlr_output, &output_box);
    toplevel_set_state(toplevel, output_box);

    // we disable all the other toplevels so they are not seen if there is transparency
    workspace_toplevels_set_enabled(workspace, false);
    // we also disable bottom and top layer surfaces, and leave only the background
    layers_under_fullscreen_set_enabled(workspace->output, false);

    // lastly, we configure the layout; it is important to call this after the nodes have been disabled, so the
    // animations are not started
    if(toplevel->prev_mode != TOPLEVEL_MODE_FLOATING) {
        layout_configure(workspace);
    }
}

void
toplevel_unset_fullscreen(struct toplevel *toplevel) {
    if(toplevel->workspace->fullscreen != toplevel)
        return;

    struct workspace *workspace = toplevel->workspace;

    workspace->fullscreen = NULL;
    toplevel->mode = toplevel->prev_mode;

    wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign_toplevel_handle->wlr_handle, false);

    if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        wl_list_insert(&workspace->floating, &toplevel->link);
        wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
        // we restack the children/parents
        toplevel_raise_to_top(toplevel);
        // before we set the state for this toplevel be sure to recheck the rules
        rules_update_for_toplevel(toplevel);

        toplevel_set_state(toplevel, toplevel->prev_deco_box);
    } else {
        if(toplevel->prev_index == -1) {
            layout_add(workspace, toplevel);
        } else {
            struct wl_list *link = toplevel->mode == TOPLEVEL_MODE_MASTER
                    ? list_at(&workspace->masters, toplevel->prev_index)
                    : list_at(&workspace->slaves, toplevel->prev_index);
            if(link != NULL) {
                wl_list_insert(link->prev, &toplevel->link);
                if(toplevel->mode == TOPLEVEL_MODE_MASTER) {
                    workspace->master_count++;
                    if(workspace->master_count > toplevel->workspace->output->master_count) {
                        demote_last_master(workspace);
                    }
                } else {
                    workspace->slave_count++;
                }
            } else {
                layout_add(workspace, toplevel);
            }
        }

        wlr_scene_node_reparent(&toplevel->scene_tree->node, server.tiled_tree);
        // before we set the state for this toplevel be sure to recheck the rules
        rules_update_for_toplevel(toplevel);

        layout_configure(workspace);
    }

    // reenable toplevels and layers
    workspace_toplevels_set_enabled(workspace, true);
    layers_under_fullscreen_set_enabled(workspace->output, true);
}

void
unfocus_focused_toplevel(void) {
    struct toplevel *toplevel = server.focused_toplevel;
    if(toplevel == NULL)
        return;

    server.focused_toplevel = NULL;

    // deactivate the surface
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
    wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign_toplevel_handle->wlr_handle, false);

    decoration_set_active(&toplevel->decoration, false);

    // clear all focus on the keyboard
    wlr_seat_keyboard_notify_clear_focus(server.seat.base);

    ipc_send_focused_toplevel();
}

void
focus_toplevel(struct toplevel *toplevel, bool jump_cursor) {
    if(server.mode > SERVER_MODE_CAN_GIVE_FOCUS || server.exclusive || toplevel == server.focused_toplevel ||
            (toplevel->workspace->fullscreen != NULL && toplevel != toplevel->workspace->fullscreen))
        return;

    // we change the workspace if needed, this is primarly because of the activation protocol
    change_workspace(toplevel->workspace, true);

    struct toplevel *prev = server.focused_toplevel;
    server.focused_toplevel = toplevel;

    if(prev != NULL) {
        wlr_xdg_toplevel_set_activated(prev->xdg_toplevel, false);
        wlr_foreign_toplevel_handle_v1_set_activated(prev->foreign_toplevel_handle->wlr_handle, false);

        rules_update_for_toplevel(prev);
        decoration_set_active(&prev->decoration, false);
    }

    // if the toplevel is floating we keep it at the beggining of the list, so we know the z-indexing
    if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        wl_list_remove(&toplevel->link);
        wl_list_insert(&toplevel->workspace->floating, &toplevel->link);
    }

    // activate the toplevel
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
    wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign_toplevel_handle->wlr_handle, true);

    rules_update_for_toplevel(toplevel);

    toplevel_raise_to_top(toplevel);
    decoration_set_active(&toplevel->decoration, true);

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat.base);
    if(keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(server.seat.base, toplevel->xdg_toplevel->base->surface, keyboard->keycodes,
                keyboard->num_keycodes, &keyboard->modifiers);
    }

    if(jump_cursor) {
        // jump to the middpoint of the toplevel
        wlr_cursor_warp(server.cursor.base, NULL, toplevel->deco_box.x + toplevel->deco_box.width / 2.0,
                toplevel->deco_box.y + toplevel->deco_box.height / 2.0);

        cursor_handle_focus(get_now_in_ms(), false);
    }

    ipc_send_focused_toplevel();
}

struct toplevel *
toplevel_find_closest_floating_on_workspace(struct toplevel *toplevel, enum direction direction) {
    struct workspace *workspace = toplevel->workspace;

    struct toplevel *min = NULL;
    int min_val = INT_MAX;

    if(direction == DIRECTION_UP) {
        int my = toplevel->deco_box.y + toplevel->deco_box.height / 2;

        struct toplevel *iter;
        wl_list_for_each(iter, &workspace->floating, link) {
            if(iter == toplevel)
                continue;

            int y = iter->deco_box.y + iter->deco_box.height / 2;

            int dy = my - y;
            if(dy > 0 && dy < min_val) {
                min = iter;
                min_val = dy;
            }
        }

        return min;
    } else if(direction == DIRECTION_DOWN) {
        int my = toplevel->deco_box.y + toplevel->deco_box.height / 2;

        struct toplevel *iter;
        wl_list_for_each(iter, &workspace->floating, link) {
            if(iter == toplevel)
                continue;

            int y = iter->deco_box.y + iter->deco_box.height / 2;

            int dy = y - my;
            if(dy > 0 && dy < min_val) {
                min = iter;
                min_val = dy;
            }
        }

        return min;
    } else if(direction == DIRECTION_LEFT) {
        int mx = toplevel->deco_box.x + toplevel->deco_box.width / 2;

        struct toplevel *iter;
        wl_list_for_each(iter, &workspace->floating, link) {
            if(iter == toplevel)
                continue;

            int x = iter->deco_box.x + iter->deco_box.width / 2;

            int dx = mx - x;
            if(dx > 0 && dx < min_val) {
                min = iter;
                min_val = dx;
            }
        }

        return min;
    } else if(direction == DIRECTION_RIGHT) {
        int mx = toplevel->deco_box.x + toplevel->deco_box.width / 2;

        struct toplevel *iter;
        wl_list_for_each(iter, &workspace->floating, link) {
            if(iter == toplevel)
                continue;

            int x = iter->deco_box.x + iter->deco_box.width / 2;

            int dx = x - mx;
            if(dx > 0 && dx < min_val) {
                min = iter;
                min_val = dx;
            }
        }

        return min;
    }

    assert(false && "unreachable");
}

struct wlr_box
toplevel_get_current_display_deco_box(struct toplevel *toplevel) {
    if(toplevel->animation != NULL) {
        return fx_transform_animation_get_current(toplevel->animation);
    }

    return toplevel->deco_box;
}

void
toplevel_get_current_display_content_size(struct toplevel *toplevel, int *width, int *height) {
    struct wlr_box deco_box = toplevel_get_current_display_deco_box(toplevel);
    *width = deco_box.width;
    *height = deco_box.height;
    decoration_get_content_size(&toplevel->decoration, width, height);
}

static void
toplevel_animation_callback(struct wlr_box current, bool done, void *user_data) {
    struct toplevel *toplevel = user_data;

    decoration_configure(&toplevel->decoration, current.width, current.height);

    int width = current.width, height = current.height;
    decoration_get_content_size(&toplevel->decoration, &width, &height);
    toplevel_clip_tree(toplevel, width, height);

    wlr_scene_node_set_position(&toplevel->scene_tree->node, current.x, current.y);

    if(done) {
        fx_transform_animation_destroy(toplevel->animation);
        toplevel->animation = NULL;
    }
}

void
toplevel_set_state(struct toplevel *toplevel, struct wlr_box deco_box) {
    int width = deco_box.width, height = deco_box.height;
    decoration_get_content_size(&toplevel->decoration, &width, &height);

    // this may have been left at true if the user was fast enough
    toplevel->should_choose_size = false;

    if(server.mode != SERVER_MODE_MOVING || toplevel != server.grabbed_toplevel) {
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
    }

    struct wlr_box current;
    if(toplevel->needs_popin_adjustment) {
        // we patch the animation for the popin effect; note: we add +1 so there is some toplevel surface shown
        current.width = toplevel->decoration.min_width + 1;
        current.height = toplevel->decoration.min_height + 1;
        current.x = deco_box.x + (deco_box.width - current.width) / 2;
        current.y = deco_box.y + (deco_box.height - current.height) / 2;

        toplevel->needs_popin_adjustment = false;
    } else if(toplevel->animation != NULL) {
        current = fx_transform_animation_get_current(toplevel->animation);
        fx_transform_animation_destroy(toplevel->animation);
        toplevel->animation = NULL;
    } else {
        current = toplevel->deco_box;
    }

    if(server.config->animations.enabled && toplevel->scene_tree->node.enabled && toplevel != server.grabbed_toplevel &&
            !(toplevel_is_tiled(toplevel) && server.mode == SERVER_MODE_RESIZING_MASTER_RATIO) &&
            !wlr_box_equal(&current, &deco_box)) {
        toplevel->animation = fx_transform_animation_create(current, deco_box, server.config->animations.duration,
                server.config->animations.curve, toplevel_animation_callback, toplevel);
    } else {
        decoration_configure(&toplevel->decoration, deco_box.width, deco_box.height);
        toplevel_clip_tree(toplevel, width, height);
        wlr_scene_node_set_position(&toplevel->scene_tree->node, deco_box.x, deco_box.y);
    }

    toplevel->deco_box = deco_box;
}

void
toplevel_floating_set_own_size(struct toplevel *toplevel) {
    assert(toplevel->mode == TOPLEVEL_MODE_FLOATING);

    toplevel->should_choose_size = true;
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
}

struct wlr_box
toplevel_get_geometry(struct toplevel *toplevel) {
    struct wlr_box geometry;
    wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);

    return geometry;
}

void
toplevel_start_move(struct toplevel *toplevel, bool by_keybind) {
    server.grabbed_toplevel = toplevel;
    server.mode = SERVER_MODE_MOVING;
    server.move_resize_by_keybind = by_keybind;

    server.grab_x = server.cursor.base->x;
    server.grab_y = server.cursor.base->y;

    rules_update_for_toplevel(toplevel);

    server.grabbed_toplevel_initial_box = toplevel_get_current_display_deco_box(toplevel);

    if(toplevel->animation != NULL) {
        // if there is an animation running we need to stop it and start the drag
        // from there. we do that be first stopping the animation, and taking the
        // current state of the toplevel as the initial toplevel box
        fx_transform_animation_destroy(toplevel->animation);
        toplevel->animation = NULL;

        // we also immediatelly set a new toplevel state because the toplevel may
        // have been streched or clipped, so it would look weird
        toplevel_set_state(toplevel, server.grabbed_toplevel_initial_box);
    }

    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.grabbed_tree);

    if(toplevel->mode == TOPLEVEL_MODE_FLOATING) {
        wl_list_remove(&toplevel->link);
    } else if(toplevel->mode == TOPLEVEL_MODE_MASTER) {
        toplevel->workspace->master_count--;
        wl_list_remove(&toplevel->link);
        if(has_slaves(toplevel->workspace)) {
            promote_last_slave(toplevel->workspace);
        }
        layout_configure(toplevel->workspace);
    } else {
        toplevel->workspace->slave_count--;
        wl_list_remove(&toplevel->link);
        layout_configure(toplevel->workspace);
    }
}

void
toplevel_start_resize(struct toplevel *toplevel, uint32_t edges, bool by_keybind) {
    server.grabbed_toplevel = toplevel;
    server.mode = SERVER_MODE_RESIZING;
    server.move_resize_by_keybind = by_keybind;

    server.grab_x = server.cursor.base->x;
    server.grab_y = server.cursor.base->y;

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

    // remove is from the list, since grabbed toplevel should not be in a workspace
    wl_list_remove(&toplevel->link);
    // and reparent the node
    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.grabbed_tree);
}

void
handle_new_toplevel(struct wl_listener *listener, void *data) {
    // this event is raised when a client creates a new toplevel
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    // allocate a toplevel for this surface
    struct toplevel *toplevel = calloc(1, sizeof(*toplevel));
    toplevel->xdg_toplevel = xdg_toplevel;
    // we keep the toplevel in this free field so we can obtain it when needed
    toplevel->xdg_toplevel->base->data = toplevel;

    // we give it the currently active workspace
    toplevel->workspace = server.active_workspace;

    // we assign it to the tiled tree, but will reparent it later if necessery
    toplevel->scene_tree = wlr_scene_tree_create(server.tiled_tree);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
    toplevel->content_tree = wlr_scene_xdg_surface_create(toplevel->scene_tree, toplevel->xdg_toplevel->base);
    // in the node we want to keep information what it represents. we do that be keeping view in user data field,
    // which is a union of all possible 'things' we can have on the screen, or more precicely, all the things that can
    // receive pointer focus
    view_create_for_node(&toplevel->scene_tree->node, VIEW_TOPLEVEL, toplevel);

    // initialize the decoration
    decoration_init(&toplevel->decoration, toplevel->content_tree);

    wlr_fractional_scale_v1_notify_scale(toplevel->xdg_toplevel->base->surface,
            toplevel->workspace->output->wlr_output->scale);
    wlr_surface_set_preferred_buffer_scale(toplevel->xdg_toplevel->base->surface,
            ceil(toplevel->workspace->output->wlr_output->scale));

    // add foreign toplevel handler
    toplevel->foreign_toplevel_handle = foreign_toplevel_handle_create(toplevel);

    toplevel->map.notify = handle_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);

    toplevel->unmap.notify = handle_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);

    toplevel->commit.notify = handle_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

    toplevel->destroy.notify = handle_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

    toplevel->request_move.notify = handle_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);

    toplevel->request_resize.notify = handle_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);

    toplevel->request_maximize.notify = handle_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);

    toplevel->request_fullscreen.notify = handle_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);

    toplevel->set_app_id.notify = handle_set_app_id;
    wl_signal_add(&xdg_toplevel->events.set_app_id, &toplevel->set_app_id);

    toplevel->set_title.notify = handle_set_title;
    wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
}
