#include "keyboard.h"

#include <assert.h>
#include <libinput.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "array.h"
#include "config.h"
#include "keybinds.h"
#include "mwc.h"

extern struct server server;

static void
handle_modifiers(struct wl_listener *listener, void *data) {
    // this event is raised when a modifier key, such as shift or alt, is pressed
    struct keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);

    server.last_used_keyboard = keyboard;
    // a seat can only have one keyboard, but this is a limitation of the wayland protocol - not wlroots. we assign all
    // connected keyboards to the same seat. you can swap out the underlying wlr_keyboard like this and wlr_seat handles
    // this transparently.
    wlr_seat_set_keyboard(server.seat.base, keyboard->wlr_keyboard);

    // send modifiers to the client
    wlr_seat_keyboard_notify_modifiers(server.seat.base, &keyboard->wlr_keyboard->modifiers);
}

static void
handle_key(struct wl_listener *listener, void *data) {
    struct keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = data;

    server.last_used_keyboard = keyboard;

    // translate libinput keycode -> xkbcommon
    int keycode = event->keycode + 8;

    const xkb_keysym_t *syms;
    int count = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = handle_change_vt_key(syms, count) || handle_keybinds(keyboard, keycode, event->state);

    if(!handled) {
        // otherwise, we pass it along to the client
        wlr_seat_set_keyboard(server.seat.base, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(server.seat.base, event->time_msec, event->keycode, event->state);
    }
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

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

static uint32_t
get_config(const char *name, struct keyboard_config *config) {
    uint32_t found = 0;
    for(struct keyboard_config *iter = array_last(server.config->keyboards); iter >= server.config->keyboards; iter--) {
        if((iter->specified & KEYBOARD_FIELD_MATCH_NAME) && strcmp(name, iter->name) != 0)
            continue;

        if(!(found & KEYBOARD_FIELD_RATE) && (iter->specified & KEYBOARD_FIELD_RATE)) {
            config->rate = iter->rate;
            found |= KEYBOARD_FIELD_RATE;
        }
        if(!(found & KEYBOARD_FIELD_DELAY) && (iter->specified & KEYBOARD_FIELD_DELAY)) {
            config->delay = iter->delay;
            found |= KEYBOARD_FIELD_DELAY;
        }
        if(!(found & KEYBOARD_FIELD_OPTIONS) && (iter->specified & KEYBOARD_FIELD_OPTIONS)) {
            config->options = iter->options;
            found |= KEYBOARD_FIELD_OPTIONS;
        }
    }

    return found;
}

void
keyboard_configure(struct keyboard *keyboard) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if(context == NULL)
        return;

    const char *name = keyboard->wlr_keyboard->base.name == NULL ? keyboard->wlr_keyboard->base.name : "";

    struct keyboard_config config;
    get_config(name, &config);

    struct xkb_rule_names rule_names = {
            .layout = server.config->keymap_layouts,
            .variant = server.config->keymap_variants,
            .options = config.options,
    };

    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &rule_names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if(keymap == NULL) {
        wlr_log(WLR_ERROR, "could not apply the desired keymap to the keyboard `%s`, backing to default", name);
        keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if(keymap == NULL) {
            wlr_log(WLR_ERROR, "could not apply the default keymap to the keyboard `%s`", name);
            return;
        }
    }

    wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);

    // create the empty state used for keymaps
    if(keyboard->empty != NULL) {
        xkb_state_unref(keyboard->empty);
    }
    keyboard->empty = xkb_state_new(keymap);

    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, config.rate, config.delay);
}

void
handle_new_keyboard(struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct keyboard *keyboard = calloc(1, sizeof(*keyboard));
    keyboard->wlr_keyboard = wlr_keyboard;

    keyboard_configure(keyboard);

    keyboard->modifiers.notify = handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

    keyboard->key.notify = handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

    keyboard->destroy.notify = handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server.seat.base, keyboard->wlr_keyboard);

    wl_list_insert(&server.keyboards, &keyboard->link);

    if(server.last_used_keyboard == NULL) {
        server.last_used_keyboard = keyboard;
    }
}
