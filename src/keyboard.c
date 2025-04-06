#include "keyboard.h"

#include "keybinds.h"
#include "mwc.h"
#include "config.h"

#include <stdbool.h>
#include <stdlib.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include <libinput.h>
#include <xkbcommon/xkbcommon.h>

extern struct mwc_server server;

static void
keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    // this event is raised when a modifier key, such as shift or alt, is pressed
    struct mwc_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);

    server.last_used_keyboard = keyboard;
    // a seat can only have one keyboard, but this is a limitation of the
    // wayland protocol - not wlroots. we assign all connected keyboards to the
    // same seat. you can swap out the underlying wlr_keyboard like this and
    // wlr_seat handles this transparently.
    wlr_seat_set_keyboard(server.seat, keyboard->wlr_keyboard);
    // send modifiers to the client
    wlr_seat_keyboard_notify_modifiers(server.seat, &keyboard->wlr_keyboard->modifiers);
}

static void
keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct mwc_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = data;

    server.last_used_keyboard = keyboard;

    // translate libinput keycode -> xkbcommon
    uint32_t keycode = event->keycode + 8;

    const xkb_keysym_t *syms;
    int count = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = handle_change_vt_key(syms, count);

    if(!handled) {
        handled = server_handle_keybinds(keyboard, keycode, event->state);
    }

    if(!handled) {
        // otherwise, we pass it along to the client
        wlr_seat_set_keyboard(server.seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(server.seat, event->time_msec, event->keycode, event->state);
    }
}

static void
keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    struct mwc_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

    if(server.last_used_keyboard == keyboard) {
        server.last_used_keyboard = NULL;
    }

    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);

    xkb_state_unref(keyboard->empty);
    free(keyboard);
}

bool
keyboard_configure(struct mwc_keyboard *keyboard) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if(context == NULL) return false;

    struct xkb_rule_names rule_names = {
        .layout = server.config->keymap_layouts,
        .variant = server.config->keymap_variants,
        .options = server.config->keymap_options,
    };

    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &rule_names,
                                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
    if(keymap == NULL) {
        wlr_log(WLR_ERROR, "could not apply the desired configuration to the keyboard");
        keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if(keymap == NULL) {
            wlr_log(WLR_ERROR, "could not apply the default configuration to the keyboard");
            return false;
        }
    }

    wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    if(keyboard->empty != NULL) {
        xkb_state_unref(keyboard->empty);
    }

    keyboard->empty = xkb_state_new(keymap);

    wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, server.config->keyboard_rate,
                                 server.config->keyboard_delay);

    return true;
}

void
server_handle_new_keyboard(struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct mwc_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    keyboard->wlr_keyboard = wlr_keyboard;

    keyboard_configure(keyboard);

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server.seat, keyboard->wlr_keyboard);

    wl_list_insert(&server.keyboards, &keyboard->link);

    if(server.last_used_keyboard == NULL) {
        server.last_used_keyboard = keyboard;
    }
}

