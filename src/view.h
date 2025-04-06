#pragma once

#include <scenefx/types/wlr_scene.h>

// all the things we are drawing on the screen
enum mwc_view_type {
    MWC_TOPLEVEL,
    MWC_POPUP,
    MWC_LAYER_SURFACE,
    MWC_LOCK_SURFACE,
    MWC_BORDER,
    MWC_TITLEBAR_BASE,
    MWC_TITLEBAR_CLOSE_BUTTON,
    MWC_TITLEBAR_TITLE,
};

struct mwc_toplevel;
struct mwc_layer_surface;
struct mwc_lock_surface;

struct mwc_view {
    enum mwc_view_type type;
    union {
        struct mwc_toplevel *toplevel;
        struct mwc_popup *popup;
        struct mwc_layer_surface *layer_surface;
        struct mwc_lock_surface *lock_surface;
        struct wlr_scene_rect *rect;
        struct text_node *text_node;
    };
};

void
view_create_for_node(struct wlr_scene_node *node, enum mwc_view_type type, void *view);

struct mwc_view *
root_parent_of_surface(struct wlr_surface *wlr_surface);

struct mwc_view *
view_at(double lx, double ly,
        struct wlr_surface **surface,
        double *sx, double *sy);

void
focus_view(struct mwc_view *view);

struct mwc_toplevel *
view_try_get_toplevel(struct mwc_view *view);

