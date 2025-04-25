#pragma once

#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>

#include "mwc.h"
#include "workspace.h"

struct output {
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

    struct workspace *active_workspace;

    struct wlr_scene_rect *session_lock_rect;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

void
server_handle_new_output(struct wl_listener *listener, void *data);

// based on the current state and the current config creates, updates or destroys blur
void
output_configure_blur(struct output *output);

void
output_place_in_layout(struct output *output);

bool
output_configure(struct wlr_output *output);

// returns `output_config *` if there is a configuration specified for output `name`,
// or `NULL` if there is no configuration specified
struct output_config *
output_find_config_by_name(char *name);

struct output *
output_get_relative(struct output *output, enum direction direction);

void
cursor_jump_output(struct output *output);

void
focus_output(struct output *output, enum direction side);

struct wlr_box
output_create_centered_box(struct output *output, uint32_t width, uint32_t height);
