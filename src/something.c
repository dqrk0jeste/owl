#include <scenefx/types/wlr_scene.h>

#include "something.h"

#include "mwc.h"
#include "layer_surface.h"
#include "popup.h"
#include "session_lock.h"

#include <assert.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>

extern struct mwc_server server;

struct mwc_something *
root_parent_of_surface(struct wlr_surface *wlr_surface) {
    struct wlr_surface *root_surface = wlr_surface_get_root_surface(wlr_surface);

    struct wlr_scene_tree *tree;
    struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(root_surface);

    if(xdg_surface != NULL) {
        tree = xdg_surface->data;
    } else {
        struct wlr_layer_surface_v1 *wlr_layer_surface =
            wlr_layer_surface_v1_try_from_wlr_surface(root_surface);
        if(wlr_layer_surface != NULL) {
            struct mwc_layer_surface *layer_surface = wlr_layer_surface->data;
            tree = layer_surface->scene->tree;
        } else {
            struct wlr_session_lock_surface_v1 *wlr_lock_surface =
                wlr_session_lock_surface_v1_try_from_wlr_surface(root_surface);
            if(wlr_lock_surface != NULL) {
                struct mwc_lock_surface *lock_surface = wlr_lock_surface->data;
                tree = lock_surface->scene_tree;
            } else {
                return NULL;
            }
        }
    }

    struct mwc_something *something = tree->node.data;
    while(something == NULL || something->type == MWC_POPUP) {
        tree = tree->node.parent;
        something = tree->node.data;
    }

    return something;
}

struct mwc_something *
something_at(double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
    /* this returns the topmost node in the scene at the given layout coords */
    struct wlr_scene_node *node = wlr_scene_node_at(&server.scene->tree.node, lx, ly, sx, sy);
    if(node == NULL) return NULL;

    if(node->type == WLR_SCENE_NODE_RECT) {
        struct mwc_toplevel *toplevel = node->parent->node.data;
        assert(toplevel != NULL);

        struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
        if(rect == toplevel->titlebar.base) {
            return &toplevel->titlebar.base_something;
        } else {
            return &toplevel->titlebar.close_button_something;
        }
    } else if(node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
        struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
        if(scene_surface == NULL) {
            return NULL;
        }

        *surface = scene_surface->surface;

        struct wlr_scene_tree *tree = node->parent;
        struct mwc_something *something = tree->node.data;
        while(something == NULL || something->type == MWC_POPUP) {
            tree = tree->node.parent;
            something = tree->node.data;
        }

        return something;
    }

    return NULL;
}

void
focus_something(struct mwc_something *something) {
    assert(something != NULL);

    switch(something->type) {
        case MWC_TOPLEVEL: {
            focus_toplevel(something->toplevel);
            return;
        }
        case MWC_LAYER_SURFACE: {
            focus_layer_surface(something->layer_surface);
            return;
        }
        case MWC_LOCK_SURFACE: {
            focus_lock_surface(something->lock_surface);
            return;
        }
        case MWC_TITLEBAR_BASE:
        case MWC_TITLEBAR_CLOSE_BUTTON: {
            struct mwc_toplevel *toplevel = something->rect->node.parent->node.data;
            focus_toplevel(toplevel);
            return;
        }
        case MWC_POPUP: {
            struct mwc_popup *popup = something->popup;
            focus_something(popup_get_root_parent(popup));
            return;
        }
    }
}

struct mwc_toplevel *
something_try_get_toplevel(struct mwc_something *something) {
    switch(something->type) {
        case MWC_TOPLEVEL: {
            return something->toplevel;
        }
        case MWC_TITLEBAR_BASE:
        case MWC_TITLEBAR_CLOSE_BUTTON: {
            return something->rect->node.parent->node.data;
        }
        case MWC_POPUP:
        case MWC_LOCK_SURFACE:
        case MWC_LAYER_SURFACE: {
            return NULL;
        }
    }
}

