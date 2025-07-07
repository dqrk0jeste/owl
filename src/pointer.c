#include "pointer.h"

#include <assert.h>
#include <bits/time.h>
#include <libinput.h>
#include <limits.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

#include "array.h"
#include "mwc.h"

extern struct server server;

static void
get_config(const char *name, struct pointer_config *config) {
    uint32_t found = 0;
    for(struct pointer_config *iter = array_last(server.config->pointers); iter >= server.config->pointers; iter--) {
        if((iter->specified & POINTER_FIELD_MATCH_NAME) && strcmp(iter->name, name) != 0)
            continue;

        if(!(found & POINTER_FIELD_ACCELERATION) && (iter->specified & POINTER_FIELD_ACCELERATION)) {
            config->acceleration = iter->acceleration;
            found |= POINTER_FIELD_ACCELERATION;
        }
        if(!(found & POINTER_FIELD_SENSITIVITY) && (iter->specified & POINTER_FIELD_SENSITIVITY)) {
            config->sensitivity = iter->sensitivity;
            found |= POINTER_FIELD_SENSITIVITY;
        }
        if(!(found & POINTER_FIELD_LEFT_HANDED) && (iter->specified & POINTER_FIELD_LEFT_HANDED)) {
            config->left_handed = iter->left_handed;
            found |= POINTER_FIELD_LEFT_HANDED;
        }
    }

    assert(found & POINTER_FIELD_ACCELERATION);
    assert(found & POINTER_FIELD_SENSITIVITY);
    assert(found & POINTER_FIELD_LEFT_HANDED);
}

void
pointer_configure(struct pointer *pointer) {
    const char *name = pointer->wlr_pointer->base.name == NULL ? pointer->wlr_pointer->base.name : "";

    if(!wlr_input_device_is_libinput(&pointer->wlr_pointer->base)) {
        wlr_log(WLR_ERROR, "could not configure the pointer `%s`, not libinput", name);
        return;
    }

    struct pointer_config config;
    get_config(name, &config);

    struct libinput_device *device = wlr_libinput_get_device_handle(&pointer->wlr_pointer->base);

    if(libinput_device_config_accel_set_speed(device, config.sensitivity) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "applying sensitivity to device `%s` failed", name);
    }

    struct libinput_config_accel *accel_config = libinput_config_accel_create(config.acceleration);
    if(accel_config == NULL ||
            libinput_device_config_accel_apply(device, accel_config) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "applying acceleration profile to device `%s` failed", name);
    }
    libinput_config_accel_destroy(accel_config);

    if(libinput_device_config_tap_get_finger_count(device) > 0) {
        // if trackpad then apply trackpad specific settings
        if(libinput_device_config_tap_set_enabled(device, server.config->trackpad.tap_to_click) !=
                LIBINPUT_CONFIG_STATUS_SUCCESS) {
            wlr_log(WLR_ERROR, "applying `tap to click` to device `%s` failed", name);
        }

        if(libinput_device_config_scroll_set_natural_scroll_enabled(device, server.config->trackpad.natural_scroll) !=
                LIBINPUT_CONFIG_STATUS_SUCCESS) {
            wlr_log(WLR_ERROR, "applying `natural scroll` to device `%s` failed", name);
        }

        if(libinput_device_config_scroll_set_method(device, server.config->trackpad.scroll_method) !=
                LIBINPUT_CONFIG_STATUS_SUCCESS) {
            wlr_log(WLR_ERROR, "applying `scroll method` to device `%s` failed", name);
        }

        if(libinput_device_config_dwt_set_enabled(device, server.config->trackpad.disable_while_typing) !=
                LIBINPUT_CONFIG_STATUS_SUCCESS) {
            wlr_log(WLR_ERROR, "applying `disable while typing` to device `%s` failed", name);
        }
    }
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct pointer *pointer = wl_container_of(listener, pointer, destroy);

    wl_list_remove(&pointer->link);
    free(pointer);
}

void
handle_new_pointer(struct wlr_input_device *device) {
    struct pointer *pointer = calloc(1, sizeof(*pointer));
    pointer->wlr_pointer = wlr_pointer_from_input_device(device);

    pointer_configure(pointer);
    wlr_cursor_attach_input_device(server.cursor.base, device);

    wl_list_insert(&server.pointers, &pointer->link);

    pointer->destroy.notify = handle_destroy;
    wl_signal_add(&pointer->wlr_pointer->base.events.destroy, &pointer->destroy);
}
