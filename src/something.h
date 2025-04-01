#pragma once

#include <scenefx/types/wlr_scene.h>

enum mwc_type {
    MWC_TOPLEVEL,
    MWC_POPUP,
    MWC_LAYER_SURFACE,
    MWC_LOCK_SURFACE,
    MWC_TITLEBAR_BASE,
    MWC_TITLEBAR_CLOSE_BUTTON,
};

struct mwc_toplevel;
struct mwc_layer_surface;
struct mwc_lock_surface;

struct mwc_something {
    enum mwc_type type;
    union {
        struct mwc_toplevel *toplevel;
        struct mwc_popup *popup;
        struct mwc_layer_surface *layer_surface;
        struct mwc_lock_surface *lock_surface;
        struct wlr_scene_rect *rect;
    };
};

struct mwc_something *
root_parent_of_surface(struct wlr_surface *wlr_surface);

struct mwc_something *
something_at(double lx, double ly,
        struct wlr_surface **surface,
        double *sx, double *sy);

void
focus_something(struct mwc_something *something);

struct mwc_toplevel *
something_try_get_toplevel(struct mwc_something *something);

