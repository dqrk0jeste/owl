#include "view.h"

#include <assert.h>
#include <scenefx/types/wlr_scene.h>
#include <wayland-util.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "layer_surface.h"
#include "mwc.h"
#include "popup.h"
#include "session_lock.h"
#include "text_node.h"

extern struct server server;

static void
view_handle_destroy(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, destroy);

    free(view);
}

void
view_create_for_node(struct wlr_scene_node *node, enum view_type type, void *thing) {
    struct view *view = calloc(1, sizeof(*view));

    view->type = type;
    // since they are all pointers its the same thing which one we set
    view->toplevel = thing;

    // we keep it in this free node data field
    node->data = view;

    // we want to free this field on the node destroy
    view->destroy.notify = view_handle_destroy;
    wl_signal_add(&node->events.destroy, &view->destroy);
}

struct view *
view_at(double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
    // this returns the topmost node in the scene at the given layout coords
    struct wlr_scene_node *node = wlr_scene_node_at(&server.scene->tree.node, lx, ly, sx, sy);
    if(node == NULL) return NULL;

    if(node->type == WLR_SCENE_NODE_RECT) {
        // if this is a rect then its either a border, a titlebar, a close button
        // or a session lock rect (we dont care about those); anyhow we return the node descriptor
        struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
        return rect->node.data;
    } else if(node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
        struct view *view = scene_buffer->node.data;
        if(view != NULL) {
            // if we desribed this node then it must be title
            assert(view->type == VIEW_TITLEBAR_TITLE);
            return view;
        } else {
            // then its just a regular surface
            struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
            if(scene_surface == NULL) {
                return NULL;
            }

            *surface = scene_surface->surface;

            // we climb the tree and find a top most node
            struct wlr_scene_tree *tree = node->parent;
            struct view *view = tree->node.data;
            while(view == NULL || view->type == VIEW_POPUP) {
                tree = tree->node.parent;
                view = tree->node.data;
            }

            return view;
        }
    }

    return NULL;
}

void
focus_view(struct view *view) {
    switch(view->type) {
        case VIEW_TOPLEVEL: {
            focus_toplevel(view->toplevel);
            return;
        }
        case VIEW_POPUP: {
            struct popup *popup = view->popup;
            focus_view(popup_get_root_parent(popup));
            return;
        }
        case VIEW_LAYER_SURFACE: {
            focus_layer_surface(view->layer_surface);
            return;
        }
        case VIEW_LOCK_SURFACE: {
            focus_lock_surface(view->lock_surface);
            return;
        }
        case VIEW_BORDER:
        case VIEW_TITLEBAR_BASE:
        case VIEW_TITLEBAR_CLOSE_BUTTON: {
            struct wlr_scene_rect *border = view->rect;
            // we climb the scene tree while there is something described
            struct wlr_scene_tree *tree = border->node.parent;
            while(tree->node.data == NULL) {
                tree = tree->node.parent;
            }

            // and focus the view
            focus_view(tree->node.data);
            return;
        }
        case VIEW_TITLEBAR_TITLE: {
            struct text_node *title = view->text_node;
            // same thing
            struct wlr_scene_tree *tree = title->scene_buffer->node.parent;
            while(tree->node.data == NULL) {
                tree = tree->node.parent;
            }

            // and focus the view
            focus_view(tree->node.data);
            return;
        }
    }
}

struct toplevel *
view_try_get_toplevel(struct view *view) {
    switch(view->type) {
        case VIEW_TOPLEVEL:
            return view->toplevel;
        case VIEW_POPUP: {
            struct popup *popup = view->popup;
            struct view *root = popup_get_root_parent(popup);
            if(root->type == VIEW_TOPLEVEL) {
                return root->toplevel;
            }
            return NULL;
        }
        case VIEW_BORDER:
        case VIEW_TITLEBAR_BASE:
        case VIEW_TITLEBAR_CLOSE_BUTTON: {
            struct wlr_scene_rect *border = view->rect;
            // we climb the scene tree while there is something described
            struct wlr_scene_tree *tree = border->node.parent;
            while(tree->node.data == NULL) {
                tree = tree->node.parent;
            }

            struct view *view = tree->node.data;
            if(view->type == VIEW_TOPLEVEL) {
                return view->toplevel;
            }
        }
        case VIEW_TITLEBAR_TITLE: {
            struct text_node *title = view->text_node;
            // same thing
            struct wlr_scene_tree *tree = title->scene_buffer->node.parent;
            while(tree->node.data == NULL) {
                tree = tree->node.parent;
            }

            struct view *view = tree->node.data;
            if(view->type == VIEW_TOPLEVEL) {
                return view->toplevel;
            }
        }
        case VIEW_LOCK_SURFACE:
        case VIEW_LAYER_SURFACE:
            return NULL;
    }
}
