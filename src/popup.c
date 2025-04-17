#include "popup.h"

#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "layer_surface.h"
#include "mwc.h"
#include "toplevel.h"
#include "view.h"

extern struct mwc_server server;

static void
popup_handle_commit(struct wl_listener *listener, void *data) {
    struct mwc_popup *popup = wl_container_of(listener, popup, commit);

    if(!popup->xdg_popup->base->initialized) return;

    if(popup->xdg_popup->base->initial_commit) {
        struct mwc_view *root = popup_get_root_parent(popup);

        if(root == NULL) {
            wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
        } else if(root->type == MWC_VIEW_TOPLEVEL) {
            struct wlr_box output_box = root->toplevel->workspace->output->usable_area;

            output_box.x -= root->toplevel->scene_tree->node.x;
            output_box.y -= root->toplevel->scene_tree->node.y;

            wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &output_box);
        } else if(root->type == MWC_VIEW_LAYER_SURFACE) {
            struct mwc_layer_surface *layer_surface = root->layer_surface;
            struct wlr_output *wlr_output = layer_surface->wlr_layer_surface->output;

            struct wlr_box output_box;
            wlr_output_layout_get_box(server.output_layout, wlr_output, &output_box);

            output_box.x -= layer_surface->scene->tree->node.x;
            output_box.y -= layer_surface->scene->tree->node.y;

            wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &output_box);
        } else {
            // i dont think this is possible, but we have it covered
            wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
        }
    }
}

static void
popup_handle_destroy(struct wl_listener *listener, void *data) {
    struct mwc_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    free(popup);
}

void
server_handle_new_popup(struct wl_listener *listener, void *data) {
    // this event is raised when a client creates a new popup
    struct wlr_xdg_popup *xdg_popup = data;

    struct mwc_popup *popup = calloc(1, sizeof(*popup));
    popup->xdg_popup = xdg_popup;
    xdg_popup->base->data = popup;

    // if there is no parent, then this popup may be reparented later
    // see layer_surface_handle_new_popup()
    if(xdg_popup->parent != NULL) {
        struct wlr_xdg_surface *parent_xdg_surface = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);

        struct wlr_scene_tree *parent_tree;
        if(parent_xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
            struct mwc_toplevel *toplevel = parent_xdg_surface->data;
            assert(toplevel != NULL);

            parent_tree = toplevel->scene_tree;
        } else if(parent_xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
            struct mwc_popup *popup = parent_xdg_surface->data;
            assert(popup != NULL);

            parent_tree = popup->scene_tree;
        } else {
            wlr_log(WLR_ERROR, "popup parent does not have a role! skipping");
            return;
        }

        if(parent_tree == NULL) {
            wlr_log(WLR_ERROR, "popup parent not mapped! skipping");
            return;
        }

        popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
        view_create_for_node(&popup->scene_tree->node, MWC_VIEW_POPUP, popup);
    }

    popup->commit.notify = popup_handle_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = popup_handle_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

struct mwc_view *
popup_get_root_parent(struct mwc_popup *popup) {
    struct wlr_scene_tree *tree = popup->scene_tree;

    struct mwc_view *view = tree->node.data;
    while(view == NULL || view->type == MWC_VIEW_POPUP) {
        tree = tree->node.parent;
        view = tree->node.data;
    }

    return view;
}
