#include "xdg_shell.h"

#include <assert.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>

#include "mwc.h"
#include "popup.h"
#include "toplevel.h"

extern struct server server;

static void
handle_request_activation(struct wl_listener *listener, void *data) {
    struct wlr_xdg_activation_v1_request_activate_event *event = data;

    struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(event->surface);
    if(xdg_surface == NULL || xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        wlr_log(WLR_ERROR, "requested activation surface is not a toplevel! skipping.");
        return;
    }

    struct toplevel *toplevel = xdg_surface->data;
    // we cannot focus toplevels that are not mapped yet
    if(!toplevel->xdg_toplevel->base->surface->mapped) {
        wlr_log(WLR_ERROR, "requested activation toplevel is not mapped yet! skipping");
        return;
    }

    focus_toplevel(toplevel, false);
}

static void
handle_new_decoration(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

    struct toplevel *toplevel = decoration->toplevel->base->data;
    toplevel->xdg_decoration = decoration;

    wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
            toplevel->client_side_decorations ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                              : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
xdg_shell_init(void) {
    // set up the xdg shell
    server.xdg_shell.base = wlr_xdg_shell_create(server.display, 6);

    server.xdg_shell.new_toplevel.notify = handle_new_toplevel;
    wl_signal_add(&server.xdg_shell.base->events.new_toplevel, &server.xdg_shell.new_toplevel);

    server.xdg_shell.new_popup.notify = handle_new_popup;
    wl_signal_add(&server.xdg_shell.base->events.new_popup, &server.xdg_shell.new_popup);

    // configures decorations
    server.xdg_shell.xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server.display);

    server.xdg_shell.new_decoration.notify = handle_new_decoration;
    wl_signal_add(&server.xdg_shell.xdg_decoration_manager->events.new_toplevel_decoration,
            &server.xdg_shell.new_decoration);

    // we also have this old protocol implementation here, since it it used by some gtk3 apps
    server.xdg_shell.kde_decoration_manager = wlr_server_decoration_manager_create(server.display);
    wlr_server_decoration_manager_set_default_mode(server.xdg_shell.kde_decoration_manager,
            WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

    // xdg activation protocol
    server.xdg_shell.xdg_activation = wlr_xdg_activation_v1_create(server.display);

    server.xdg_shell.request_activation.notify = handle_request_activation;
    wl_signal_add(&server.xdg_shell.xdg_activation->events.request_activate, &server.xdg_shell.request_activation);
}
