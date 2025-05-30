#include "mwc.h"

#include <assert.h>
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
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>

#include "array.h"
#include "config.h"
#include "constraint.h"
#include "cursor.h"
#define STRING_IMPLEMENTATION
#include "dyn_string.h"
#include "font.h"
#include "gamma_control.h"
#include "helpers.h"
#include "ipc.h"
#include "output.h"
#include "session_lock.h"

// we initialize an instance of our global state
struct server server = {0};

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

// returns a default config path, does not need to be freed
static char *
get_default_config_path(void) {
    char *path = getenv("DEFAULT_CONFIG_PATH");

    if(path == NULL) {
        path = "/usr/share/mwc/default.conf";
        wlr_log(WLR_INFO, "no env DEFAULT_CONFIG_PATH set, assuming `%s`", path);
    } else {
        wlr_log(WLR_INFO, "env DEFAULT_CONFIG_PATH set to `%s`, using it", path);
    }

    return path;
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

    // todo: do arg parsing the unix way
    if(argc > 1) {
        if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("usage: mwc [-d]\n\n"
                   "\t-d, --debug       enable debug level logging"
                   "\t-v, --version     get the current version and quit");

            return 0;
        } else if(strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--debug") == 0) {
            log_level = WLR_DEBUG;
        }
    }

    int ret = 0;

    // initialize all the systems
    mkdir("/tmp/mwc", 0777);
    init_logs(log_level);

    server.config_path = get_config_path();
    if(server.config_path == NULL || (server.config = config_load(server.config_path)) == NULL) {
        wlr_log(WLR_ERROR, "couldn't open the config file, backing to default config");
        server.config = config_load(get_default_config_path());
        if(server.config == NULL) {
            wlr_log(WLR_ERROR, "couldn't open the default config file, quitting");
            ret = 1;
            goto config_path;
        }
    }

    // the wayland display is managed by libwayland. it handles accepting clients from the unix socket, manging
    // wayland globals, and so on
    server.display = wl_display_create();
    if(server.display == NULL) {
        wlr_log(WLR_ERROR, "failed to create the display");
        ret = 1;
        goto config;
    }

    server.event_loop = wl_display_get_event_loop(server.display);

    // the backend is a wlroots feature which abstracts the underlying input and output hardware. the autocreate
    // option will choose the most suitable backend based on the current environment, such as opening an x11 window
    // if an x11 server is running
    server.backend = wlr_backend_autocreate(server.event_loop, &server.session);
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

    wlr_renderer_init_wl_display(server.renderer, server.display);

    // autocreates an allocator for us. the allocator is the bridge between the renderer and the backend. it handles
    // the buffer creation, allowing wlroots to render onto the screen
    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if(server.allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create the allocator");
        ret = 1;
        goto renderer;
    }

    // create all the systems and attach listeners
    wlr_compositor_create(server.display, 6, server.renderer);
    wlr_subcompositor_create(server.display);

    // outputs
    wl_list_init(&server.outputs);

    server.new_output.notify = handle_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    server.output_layout = wlr_output_layout_create(server.display);

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
    server.drag_icon_tree = wlr_scene_tree_create(&server.scene->tree);

    // set the initial blur params
    wlr_scene_set_blur_data(server.scene, server.config->blur.params);

    font_manager_init();
    if(server.config->titlebar.title.fonts != NULL) {
        server.title_font = font_create(array_len(server.config->titlebar.title.fonts),
                server.config->titlebar.title.fonts, server.config->titlebar.title.size);
    }

    seat_init();
    cursor_init();

    xdg_shell_init();
    layer_shell_init();

    wlr_data_device_manager_create(server.display);
    wlr_data_control_manager_v1_create(server.display);
    wlr_fractional_scale_manager_v1_create(server.display, 1);
    wlr_xdg_output_manager_v1_create(server.display, server.output_layout);
    wlr_viewporter_create(server.display);
    wlr_presentation_create(server.display, server.backend);
    wlr_screencopy_manager_v1_create(server.display);
    wlr_export_dmabuf_manager_v1_create(server.display);

    server.foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(server.display);

    gamma_manager_init();
    lock_manager_init();
    relative_pointer_manager_init();
    constraint_manager_init();

    // todo: these need some wiring i think
    wlr_virtual_pointer_manager_v1_create(server.display);
    wlr_virtual_keyboard_manager_v1_create(server.display);

    fx_animation_manager_init(server.display, server.scene);
    server.animation_curve = fx_animation_curve_create(server.config->animations.curve);

    // Add a unix socket to the wayland display
    const char *socket = wl_display_add_socket_auto(server.display);
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
            wl_event_loop_add_signal(server.event_loop, SIGCHLD, sigchld_handler, NULL);
    struct wl_event_source *sigpipe_source =
            wl_event_loop_add_signal(server.event_loop, SIGPIPE, sigpipe_handler, NULL);

    ipc_init();

    if(server.config_path != NULL) {
        char *config_dir = get_parent_dir_path(server.config_path);
        config_watcher_init(config_dir);
        string_destroy(config_dir);
    }

    // run the startup commands
    for(size_t i = 0; i < array_len(server.config->on_startup); i++) {
        run_cmd(server.config->on_startup[i]);
    }

    // run the wayland event loop
    wlr_log(WLR_INFO, "running mwc on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server.display);

    // after the loop returns cleanup
    if(ipc_running()) {
        ipc_deinit();
    }

    if(config_watcher_running()) {
        config_watcher_deinit();
    }

    font_manager_deinit();

    wl_event_source_remove(sigchld_source);
    wl_event_source_remove(sigpipe_source);

    // destroy server resources
    wl_display_destroy_clients(server.display);
scene:
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor.theme_manager);
    wlr_cursor_destroy(server.cursor.base);
    wlr_allocator_destroy(server.allocator);
renderer:
    wlr_renderer_destroy(server.renderer);
backend:
    wlr_backend_destroy(server.backend);
display:
    wl_display_destroy(server.display);
config:
    config_destroy(server.config);
config_path:
    if(server.config_path != NULL) {
        string_destroy(server.config_path);
    }

    return ret;
}
