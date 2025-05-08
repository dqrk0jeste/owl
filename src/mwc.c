#include "mwc.h"

#include <assert.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <pthread.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>

#include "array.h"
#include "config.h"
#include "cursor.h"
#include "dnd.h"
#define STRING_IMPLEMENTATION
#include "dyn_string.h"
#include "gamma_control.h"
#include "helpers.h"
#include "ipc.h"
#include "keyboard.h"
#include "layer_surface.h"
#include "output.h"
#include "pointer.h"
#include "popup.h"
#include "session_lock.h"
#include "toplevel.h"
#include "wlr/backend.h"
#include "wlr/render/allocator.h"
#include "wlr/types/wlr_compositor.h"
#include "wlr/types/wlr_cursor.h"
#include "wlr/types/wlr_data_control_v1.h"
#include "wlr/types/wlr_data_device.h"
#include "wlr/types/wlr_foreign_toplevel_management_v1.h"
#include "wlr/types/wlr_screencopy_v1.h"
#include "wlr/types/wlr_seat.h"
#include "wlr/types/wlr_subcompositor.h"
#include "wlr/types/wlr_viewporter.h"
#include "wlr/types/wlr_xcursor_manager.h"
#include "wlr/types/wlr_xdg_decoration_v1.h"
#include "wlr/types/wlr_xdg_output_v1.h"
#include "wlr/util/log.h"

// we initialize an instance of our global state
struct server server = {0};

void
server_handle_new_input(struct wl_listener *listener, void *data) {
    struct wlr_input_device *input = data;

    switch(input->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_handle_new_keyboard(input);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            server_handle_new_pointer(input);
            break;
        default:
            // mwc doesnt support touch devices, drawing tablets etc
            break;
    }

    // we need to let the wlr_seat know what our capabilities are, which is
    // communiciated to the client. we always have a cursor, even if
    // there are no pointer devices, so we always include that capability
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if(!wl_list_empty(&server.keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }

    wlr_seat_set_capabilities(server.seat, caps);
}

void
server_handle_request_set_selection(struct wl_listener *listener, void *data) {
    // this event is raised by the seat when a client wants to set the selection, usually when the user copies something
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server.seat, event->source, event->serial);
}

static void
init_logs(enum wlr_log_importance log_level) {
    // open or create the log file
    int log_fd = open("/tmp/mwc/logs", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(log_fd > 0) {
        // redirect the logs to the log file
        dup2(log_fd, 1);
        dup2(log_fd, 2);
        close(log_fd);
    }

    wlr_log_init(log_level, NULL);
}

// handles exits of child processes
static int
sigchld_handler(int signal_number, void *data) {
    while(waitpid(-1, NULL, WNOHANG) > 0) {
        // do nothing
    }

    return 0;
}

// dont crash on broken pipe
static int
sigpipe_handler(int signal_number, void *data) {
    // do nothing
    return 0;
}

static char *
get_config_path(void) {
    char *env_path = getenv("MWC_CONFIG_PATH");
    if(env_path != NULL) {
        wlr_log(WLR_INFO, "env CONFIG_PATH set to `%s`, using it", env_path);
        return string_new(env_path);
    }

    char *config_home = getenv("XDG_CONFIG_HOME");
    if(config_home != NULL) {
        char *path = string_new(config_home);
        string_append_c_string(&path, "/mwc/mwc.conf");
        return path;
    }

    char *home = getenv("HOME");
    if(home != NULL) {
        char *path = string_new(home);
        string_append_c_string(&path, "/.config/mwc/mwc.conf");
        return path;
    }

    return NULL;
}

static char *
get_parent_dir_path(char *path) {
    size_t len = string_len(path);
    while(len >= 0 && path[len] != '/')
        len--;

    if(len < 0) {
        return NULL;
    }

    return string_substring(path, 0, len);
}

int
main(int argc, char *argv[]) {
    enum wlr_log_importance log_level = WLR_INFO;
    if(argc > 1) {
        if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("usage: mwc [-d]\n\n"
                   "\t-d (--debug) - enable debug level logging");

            return 0;
        } else if(strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--debug") == 0) {
            log_level = WLR_DEBUG;
        }
    }

    int ret = 0;

    // initialize all the systems
    mkdir("/tmp/mwc", 0777);
    init_logs(log_level);

    // initialize the font library before parsing the initial config file
    fcft_init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_INFO);

    server.config_path = get_config_path();
    server.config = config_load(server.config_path);
    if(server.config == NULL) {
        ret = 1;
        goto fcft;
    }

    // the wayland display is managed by libwayland. it handles accepting clients from the unix socket, manging
    // wayland globals, and so on
    server.wl_display = wl_display_create();
    if(server.wl_display == NULL) {
        wlr_log(WLR_ERROR, "failed to create the display");
        ret = 1;
        goto config;
    }

    server.wl_event_loop = wl_display_get_event_loop(server.wl_display);

    // the backend is a wlroots feature which abstracts the underlying input and output hardware. the autocreate
    // option will choose the most suitable backend based on the current environment, such as opening an x11 window
    // if an x11 server is running
    server.backend = wlr_backend_autocreate(server.wl_event_loop, &server.session);
    if(server.backend == NULL) {
        wlr_log(WLR_ERROR, "failed to create the backend");
        ret = 1;
        goto display;
    }

    server.renderer = fx_renderer_create(server.backend);
    if(server.renderer == NULL) {
        wlr_log(WLR_ERROR, "failed to create the renderer");
        ret = 1;
        goto backend;
    }

    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    // autocreates an allocator for us. the allocator is the bridge between the renderer and the backend. it handles
    // the buffer creation, allowing wlroots to render onto the screen
    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if(server.allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create the allocator");
        ret = 1;
        goto renderer;
    }

    // create all the systems and attach listeners
    wlr_compositor_create(server.wl_display, 6, server.renderer);
    wlr_subcompositor_create(server.wl_display);

    wlr_data_device_manager_create(server.wl_display);

    // creates an output layout, which a wlroots utility for working with an arrangement of screens in a physical
    // layout
    server.output_layout = wlr_output_layout_create(server.wl_display);

    wl_list_init(&server.outputs);

    // configure a listener to be notified when new outputs are available on the backend
    server.new_output.notify = server_handle_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    // create a scene graph. this is a wlroots abstraction that handles all rendering and damage tracking. all the
    // compositor author needs to do is add things that should be rendered to the scene graph at the proper
    // positions and then call wlr_scene_output_commit() to render a frame
    server.scene = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

    // create all the scene trees in the correct order
    server.background_tree = wlr_scene_tree_create(&server.scene->tree);
    server.bottom_tree = wlr_scene_tree_create(&server.scene->tree);
    server.tiled_tree = wlr_scene_tree_create(&server.scene->tree);
    server.floating_tree = wlr_scene_tree_create(&server.scene->tree);
    server.top_tree = wlr_scene_tree_create(&server.scene->tree);
    server.fullscreen_tree = wlr_scene_tree_create(&server.scene->tree);
    server.grabbed_tree = wlr_scene_tree_create(&server.scene->tree);
    server.overlay_tree = wlr_scene_tree_create(&server.scene->tree);
    server.session_lock_tree = wlr_scene_tree_create(&server.scene->tree);

    // set the initial blur params
    wlr_scene_set_blur_data(server.scene, server.config->blur_params);

    // set up the xdg shell
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 6);
    server.new_toplevel.notify = server_handle_new_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_toplevel);
    server.new_popup.notify = server_handle_new_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_popup);

    // set up the layer shell
    server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
    server.new_layer_surface.notify = server_handle_new_layer_surface;
    wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

    // creates a cursor, which is a wlroots utility for tracking the cursor image shown on screen.
    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

    server.cursor_mgr = wlr_xcursor_manager_create(server.config->cursor_theme, server.config->cursor_size);
    // we also add xcursor theme env variables
    cursor_set_xcursor_variables(server.config->cursor_theme, server.config->cursor_size);

    // todo: move these into cursor.c
    server.cursor_motion.notify = server_handle_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    server.cursor_motion_absolute.notify = server_handle_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
    server.cursor_button.notify = server_handle_cursor_button;
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);
    server.cursor_axis.notify = server_handle_cursor_axis;
    wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
    server.cursor_frame.notify = server_handle_cursor_frame;
    wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

    server.new_input.notify = server_handle_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);

    wl_list_init(&server.keyboards);
    wl_list_init(&server.pointers);

    // configures a seat, which is a single "seat" at which a user sits and operates the computer. this conceptually
    // includes up to one keyboard, pointer, touch, and drawing tablet device.
    server.seat = wlr_seat_create(server.wl_display, "seat0");

    server.request_cursor.notify = cursor_handle_request;
    wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
    server.request_set_selection.notify = server_handle_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);

    // configure drag presentation and listeners
    server.drag_icon_tree = wlr_scene_tree_create(&server.scene->tree);
    wlr_scene_node_set_enabled(&server.drag_icon_tree->node, false);

    server.request_drag.notify = server_handle_request_drag;
    wl_signal_add(&server.seat->events.request_start_drag, &server.request_drag);
    server.request_start_drag.notify = server_handle_request_start_drag;
    wl_signal_add(&server.seat->events.start_drag, &server.request_start_drag);
    server.request_destroy_drag.notify = server_handle_destroy_drag;
    // we dont wire this one up since it is wired per drag

    // handles clipboard clients
    wlr_data_control_manager_v1_create(server.wl_display);

    // configures decorations
    server.xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server.wl_display);
    server.request_xdg_decoration.notify = server_handle_request_xdg_decoration;
    wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration, &server.request_xdg_decoration);

    server.kde_decoration_manager = wlr_server_decoration_manager_create(server.wl_display);
    wlr_server_decoration_manager_set_default_mode(server.kde_decoration_manager,
            server.config->client_side_decorations ? WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT
                                                   : WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

    wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
    wlr_viewporter_create(server.wl_display);
    wlr_presentation_create(server.wl_display, server.backend);

    wlr_screencopy_manager_v1_create(server.wl_display);
    wlr_export_dmabuf_manager_v1_create(server.wl_display);
    server.foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(server.wl_display);

    wlr_fractional_scale_manager_v1_create(server.wl_display, 1);

    // todo: these need some wiring i think
    wlr_virtual_pointer_manager_v1_create(server.wl_display);
    wlr_virtual_keyboard_manager_v1_create(server.wl_display);

    server.gamma_control_manager = wlr_gamma_control_manager_v1_create(server.wl_display);
    server.set_gamma.notify = gamma_control_set_gamma;
    wl_signal_add(&server.gamma_control_manager->events.set_gamma, &server.set_gamma);

    server.session_lock_manager = wlr_session_lock_manager_v1_create(server.wl_display);
    server.new_lock.notify = session_lock_manager_handle_new;
    server.lock_manager_destroy.notify = session_lock_manager_handle_destroy;
    wl_signal_add(&server.session_lock_manager->events.new_lock, &server.new_lock);
    wl_signal_add(&server.session_lock_manager->events.destroy, &server.lock_manager_destroy);

    server.cursor_shape_manager = wlr_cursor_shape_manager_v1_create(server.wl_display, 1);
    server.request_cursor_shape.notify = cursor_shape_manager_handle_request;
    server.cursor_shape_manager_destroy.notify = cursor_shape_manager_handle_destroy;
    wl_signal_add(&server.cursor_shape_manager->events.request_set_shape, &server.request_cursor_shape);
    wl_signal_add(&server.cursor_shape_manager->events.destroy, &server.cursor_shape_manager_destroy);

    server.relative_pointer_manager = wlr_relative_pointer_manager_v1_create(server.wl_display);
    server.relative_pointer_manager_destroy.notify = server_handle_relative_pointer_manager_destroy;
    wl_signal_add(&server.relative_pointer_manager->events.destroy, &server.relative_pointer_manager_destroy);

    server.pointer_contrains_manager = wlr_pointer_constraints_v1_create(server.wl_display);
    server.new_contraint.notify = server_handle_new_constraint;
    wl_signal_add(&server.pointer_contrains_manager->events.new_constraint, &server.new_contraint);

    server.xdg_activation = wlr_xdg_activation_v1_create(server.wl_display);
    server.xdg_activation_request.notify = xdg_activation_handle_request;
    wl_signal_add(&server.xdg_activation->events.request_activate, &server.xdg_activation_request);

    fx_animation_manager_init(server.wl_display, server.scene);

    // Add a unix socket to the wayland display
    const char *socket = wl_display_add_socket_auto(server.wl_display);
    if(!socket) {
        wlr_log(WLR_ERROR, "failed to add a socket");
        ret = 1;
        goto scene;
    }

    // start the backend
    if(!wlr_backend_start(server.backend)) {
        wlr_log(WLR_ERROR, "failed to start the backend");
        ret = 1;
        goto scene;
    }

    // set the WAYLAND_DISPLAY environment variable to our socket
    setenv("WAYLAND_DISPLAY", socket, true);

    // add other relevant fds and signal handlers to the main loop
    struct wl_event_source *sigchld_source =
            wl_event_loop_add_signal(server.wl_event_loop, SIGCHLD, sigchld_handler, NULL);
    struct wl_event_source *sigpipe_source =
            wl_event_loop_add_signal(server.wl_event_loop, SIGPIPE, sigpipe_handler, NULL);

    ipc_init();

    char *config_dir = get_parent_dir_path(server.config_path);
    config_watcher_init(config_dir);
    string_destroy(config_dir);

    // run the startup commands
    for(size_t i = 0; i < array_len(server.config->run); i++) {
        run_cmd(server.config->run[i]);
    }

    // run the wayland event loop
    wlr_log(WLR_INFO, "running mwc on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server.wl_display);

    // after the loop returns cleanup
    if(ipc_running()) {
        ipc_deinit();
    }

    if(config_watcher_running()) {
        config_watcher_deinit();
    }

    wl_event_source_remove(sigchld_source);
    wl_event_source_remove(sigpipe_source);

    // destroy server resources
    wl_display_destroy_clients(server.wl_display);
scene:
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
renderer:
    wlr_renderer_destroy(server.renderer);
backend:
    wlr_backend_destroy(server.backend);
display:
    wl_display_destroy(server.wl_display);
config:
    config_destroy(server.config);
fcft:
    fcft_fini();

    return ret;
}
