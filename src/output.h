#pragma once

#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>

#include "helpers.h"
#include "workspace.h"

struct output {
    struct wl_list link;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;

    struct wl_list workspaces;
    struct workspace *active_workspace;

    int master_count;

    struct wlr_box usable_area;

    struct {
        struct wl_list background;
        struct wl_list bottom;
        struct wl_list top;
        struct wl_list overlay;
    } layers;

    struct wlr_scene_optimized_blur *blur;
    struct wlr_scene_rect *session_lock_rect;

    struct wl_listener frame;
    struct wl_listener destroy;
};

void
handle_new_output(struct wl_listener *listener, void *data);

void
output_configure(struct output *output, bool initial);

struct output *
output_get_relative(struct output *output, enum direction direction, int x, int y);

void
jump_cursor_to_output(struct output *output);

void
focus_output(struct output *output, enum direction direction);

struct wlr_box
output_create_centered_box(struct output *output, int width, int height);
