#pragma once

#include <wayland-server-core.h>

struct cursor {
    struct wlr_cursor *base;

    struct wlr_xcursor_manager *theme_manager;

    bool is_hidden;
    struct wl_event_source *hide_timer;

    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener frame;

    struct wl_listener set_cursor;
};

void
cursor_set_image(const char *name);

void
cursor_set_theme(char *theme, int size);

void
cursor_stop_move_resize(void);

void
cursor_handle_focus(uint32_t time, bool handle_keyboard_focus);

struct output *
cursor_get_output(void);

struct view *
cursor_get_view(void);

struct toplevel *
cursor_get_toplevel(void);

void
cursor_warp_output(struct output *output);

void
cursor_warp_toplevel(struct toplevel *toplevel, struct output *from_output);

void
cursor_init(void);
