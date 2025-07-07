#pragma once

#include <scenefx/types/wlr_scene.h>

// all the things we are drawing on the screen
enum view_type {
    VIEW_TOPLEVEL,
    VIEW_POPUP,
    VIEW_LAYER_SURFACE,
    VIEW_LOCK_SURFACE,
    VIEW_BORDER,
    VIEW_TITLEBAR_BASE,
    VIEW_TITLEBAR_CLOSE_BUTTON,
    VIEW_TITLEBAR_TITLE,
};

struct toplevel;
struct layer_surface;
struct lock_surface;

struct view {
    enum view_type type;
    union {
        struct toplevel *toplevel;
        struct popup *popup;
        struct layer_surface *layer_surface;
        struct lock_surface *lock_surface;
        struct wlr_scene_rect *border;
        struct wlr_scene_rect *titlebar_base;
        struct wlr_scene_rect *titlebar_close_button;
        struct text_node *titlebar_title;
    };

    // we listen for the node destroy signal so we can free this struct
    struct wl_listener destroy;
};

void
view_create_for_node(struct wlr_scene_node *node, enum view_type type, void *view);

struct view *
view_at(double lx, double ly, struct wlr_surface **surface, double *sx, double *sy);

void
focus_view(struct view *view);

struct toplevel *
view_try_get_toplevel(struct view *view);
