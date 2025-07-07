#pragma once

#include <wayland-server-core.h>

#include "keyboard.h"

typedef void (*keybind_action_func_t)(void *);

struct keybind {
    uint32_t modifiers;
    int key;

    bool active;
    keybind_action_func_t action;
    keybind_action_func_t stop;
    void *data;

    struct wl_list link;
};

bool
handle_keybinds(struct keyboard *keyboard, int keycode, enum wl_keyboard_key_state state);

bool
handle_change_vt_key(const xkb_keysym_t *keysyms, size_t count);

void
keybind_stop_server(void *data);

void
keybind_run(void *data);

void
keybind_change_workspace(void *data);

void
keybind_next_workspace(void *data);

void
keybind_prev_workspace(void *data);

void
keybind_move_to_workspace(void *data);

void
keybind_start_resize(void *data);

void
keybind_stop_resize(void *data);

void
keybind_start_move(void *data);

void
keybind_stop_move(void *data);

void
keybind_close(void *data);

void
keybind_move_focus(void *data);

void
keybind_move(void *data);

void
keybind_toggle_floating(void *data);

void
keybind_toggle_fullscreen(void *data);

void
keybind_toggle_fake_fullscreen(void *data);

void
keybind_adjust_master_ratio(void *data);

void
keybind_set_master_ratio(void *data);
