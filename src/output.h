#pragma once

#include <scenefx/types/wlr_scene.h>

#include <wlr/types/wlr_output.h>

#include "workspace.h"
#include "mwc.h"

struct mwc_output {
    struct wl_list link;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wl_list workspaces;
    struct wlr_box usable_area;

    struct {
        struct wl_list background;
        struct wl_list bottom;
        struct wl_list top;
        struct wl_list overlay;
    } layers;

    struct wlr_scene_optimized_blur *blur;

    struct mwc_workspace *active_workspace;

    struct wlr_scene_rect *session_lock_rect;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

void
server_handle_new_output(struct wl_listener *listener, void *data);

struct wlr_box
output_add_to_layout(struct mwc_output *output, struct output_config *config);

bool
output_initialize(struct wlr_output *output, struct output_config *config);

struct mwc_output *
output_get_relative(struct mwc_output *output, enum mwc_direction direction);

void
cursor_jump_output(struct mwc_output *output);

void
focus_output(struct mwc_output *output,
        enum mwc_direction side);


struct wlr_box
output_create_centered_box(struct mwc_output *output, uint32_t width, uint32_t height);

