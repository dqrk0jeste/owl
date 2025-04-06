#pragma once

#include <wlr/types/wlr_keyboard.h>

struct mwc_keyboard {
    struct wl_list link;
    struct wlr_keyboard *wlr_keyboard;
    // used for getting raw keysyms for keybinds
    struct xkb_state *empty;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

void
server_handle_new_keyboard(struct wlr_input_device *device);

bool
keyboard_configure(struct mwc_keyboard *keyboard);

