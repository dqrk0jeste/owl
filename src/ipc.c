#include "ipc.h"

#include <assert.h>
#include <json-c/json_object.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "array.h"
#include "layer_shell.h"
#include "mwc.h"
#include "output.h"
#include "parser.h"
#include "workspace.h"

extern struct server server;

// {
//      "title": string,
//      "app_id": string,
//      "workspace": uint,
//      "mode": "floating" | "master" | "slave" | "fullscreen",
// }
static json_object *
toplevel_json(struct toplevel *toplevel) {
    struct json_object *object = json_object_new_object();

    json_object_object_add(object, "title",
            toplevel->xdg_toplevel->title == NULL ? NULL : json_object_new_string(toplevel->xdg_toplevel->title));
    json_object_object_add(object, "app_id",
            toplevel->xdg_toplevel->app_id == NULL ? NULL : json_object_new_string(toplevel->xdg_toplevel->app_id));

    json_object_object_add(object, "workspace", json_object_new_uint64(toplevel->workspace->index));

    char *mode;
    if(toplevel->mode == TOPLEVEL_MODE_FLOATING)
        mode = "floating";
    else if(toplevel->mode == TOPLEVEL_MODE_MASTER)
        mode = "master";
    else if(toplevel->mode == TOPLEVEL_MODE_SLAVE)
        mode = "slave";
    else if(toplevel->mode == TOPLEVEL_MODE_FULLSCREEN)
        mode = "fullscreen";
    else
        assert(false && "unreachable");

    json_object_object_add(object, "mode", json_object_new_string(mode));

    return object;
}

// {
//      "index": uint,,
//      "output": string,
// }
static json_object *
workspace_json(struct workspace *workspace) {
    struct json_object *object = json_object_new_object();

    json_object_object_add(object, "index", json_object_new_uint64(workspace->index));
    json_object_object_add(object, "output", json_object_new_string(workspace->output->wlr_output->name));

    return object;
}

// {
//      "name": string,,
//      "current_mode": string,
//      "available_modes": string[],
// }
static json_object *
output_json(struct output *output) {
    struct json_object *object = json_object_new_object();

    json_object_object_add(object, "name", json_object_new_string(output->wlr_output->name));
    json_object_object_add(object, "description",
            output->wlr_output->description == NULL ? NULL : json_object_new_string(output->wlr_output->name));

    struct wlr_output_mode *mode = output->wlr_output->current_mode;
    char mode_string[64];
    snprintf(mode_string, sizeof(mode_string), "%dx%d@%dHz", mode->width, mode->height, mode->refresh / 1000);

    json_object_object_add(object, "current_mode", json_object_new_string(mode_string));

    struct json_object *modes = json_object_new_array();
    wl_list_for_each(mode, &output->wlr_output->modes, link) {
        snprintf(mode_string, sizeof(mode_string), "%dx%d@%dHz", mode->width, mode->height, mode->refresh / 1000);
        json_object_array_add(modes, json_object_new_string(mode_string));
    }

    json_object_object_add(object, "available_modes", modes);

    return object;
}

// {
//      "namespace": string,,
//      "output": string,
//      "layer": "background" | "bottom" | "top" | "overlay",
// }
static json_object *
layer_json(struct layer_surface *layer_surface) {
    struct json_object *object = json_object_new_object();

    json_object_object_add(object, "namespace",
            layer_surface->wlr_layer_surface->namespace == NULL
                    ? NULL
                    : json_object_new_string(layer_surface->wlr_layer_surface->namespace));

    json_object_object_add(object, "output", json_object_new_string(layer_surface->wlr_layer_surface->output->name));

    enum zwlr_layer_shell_v1_layer layer = layer_surface->wlr_layer_surface->current.layer;
    char *layer_string;

    if(layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
        layer_string = "background";
    } else if(layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) {
        layer_string = "bottom";
    } else if(layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
        layer_string = "top";
    } else if(layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
        layer_string = "overylay";
    } else {
        assert(false && "unrechable");
    }

    json_object_object_add(object, "layer", json_object_new_string(layer_string));

    return object;
}

static void
handle_get(int fd, char *what) {
    if(strcmp(what, "focused_toplevel") == 0) {
        if(server.focused_toplevel == NULL) {
            write(fd, "null", strlen("null"));
            return;
        }

        struct json_object *toplevel = toplevel_json(server.focused_toplevel);

        size_t len;
        const char *stringified = json_object_to_json_string_length(toplevel, JSON_C_TO_STRING_PRETTY, &len);

        write(fd, stringified, len);

        json_object_put(toplevel);
    } else if(strcmp(what, "focused_layer") == 0) {
        if(server.focused_layer_surface == NULL) {
            write(fd, "null", strlen("null"));
            return;
        }

        struct json_object *layer = layer_json(server.focused_layer_surface);

        size_t len;
        const char *stringified = json_object_to_json_string_length(layer, JSON_C_TO_STRING_PRETTY, &len);

        write(fd, stringified, len);

        json_object_put(layer);
    } else if(strcmp(what, "active_workspace") == 0) {
        struct json_object *workspace = workspace_json(server.active_workspace);

        size_t len;
        const char *stringified = json_object_to_json_string_length(workspace, JSON_C_TO_STRING_PRETTY, &len);

        write(fd, stringified, len);

        json_object_put(workspace);
    } else if(strcmp(what, "toplevels") == 0) {
        struct json_object *array = json_object_new_array();

        struct output *iter_output;
        wl_list_for_each(iter_output, &server.outputs, link) {
            struct workspace *iter_workspace;
            wl_list_for_each(iter_workspace, &iter_output->workspaces, link) {
                struct toplevel *iter_toplevel;
                wl_list_for_each(iter_toplevel, &iter_workspace->floating, link) {
                    json_object_array_add(array, toplevel_json(iter_toplevel));
                }
                wl_list_for_each(iter_toplevel, &iter_workspace->masters, link) {
                    json_object_array_add(array, toplevel_json(iter_toplevel));
                }
                wl_list_for_each(iter_toplevel, &iter_workspace->slaves, link) {
                    json_object_array_add(array, toplevel_json(iter_toplevel));
                }

                if(iter_workspace->fullscreen != NULL) {
                    json_object_array_add(array, toplevel_json(iter_toplevel));
                }
            }
        }

        if(server.grabbed_toplevel != NULL) {
            json_object_array_add(array, toplevel_json(server.grabbed_toplevel));
        }

        size_t len;
        const char *stringified = json_object_to_json_string_length(array, JSON_C_TO_STRING_PRETTY, &len);

        write(fd, stringified, len);

        json_object_put(array);
    } else if(strcmp(what, "layers") == 0) {
        struct json_object *array = json_object_new_array();

        struct output *iter_output;
        wl_list_for_each(iter_output, &server.outputs, link) {
            struct layer_surface *iter_layer;
            for(size_t i = 0; i < 4; i++) {
                wl_list_for_each(iter_layer, &(&iter_output->layers.background)[i], link) {
                    json_object_array_add(array, layer_json(iter_layer));
                }
            }
        }

        size_t len;
        const char *stringified = json_object_to_json_string_length(array, JSON_C_TO_STRING_PRETTY, &len);

        write(fd, stringified, len);

        json_object_put(array);
    } else if(strcmp(what, "workspaces") == 0) {
        struct json_object *array = json_object_new_array();

        struct output *iter_output;
        wl_list_for_each(iter_output, &server.outputs, link) {
            struct workspace *iter_workspace;
            wl_list_for_each(iter_workspace, &iter_output->workspaces, link) {
                json_object_array_add(array, workspace_json(iter_workspace));
            }
        }

        size_t len;
        const char *stringified = json_object_to_json_string_length(array, JSON_C_TO_STRING_PRETTY, &len);

        write(fd, stringified, len);

        json_object_put(array);
    } else if(strcmp(what, "outputs") == 0) {
        struct json_object *array = json_object_new_array();

        struct output *iter_output;
        wl_list_for_each(iter_output, &server.outputs, link) {
            json_object_array_add(array, output_json(iter_output));
        }

        size_t len;
        const char *stringified = json_object_to_json_string_length(array, JSON_C_TO_STRING_PRETTY, &len);

        write(fd, stringified, len);

        json_object_put(array);
    } else {
        write(fd, "null", strlen("null"));
    }

    close(fd);
}

static void
handle_watch(int fd, char *what) {
    if(strcmp(what, "active_workspace") == 0) {
        array_push(&server.ipc.watching_workspace, fd);
        ipc_send_active_workspace();
    } else if(strcmp(what, "focused_toplevel") == 0) {
        array_push(&server.ipc.watching_toplevel, fd);
        ipc_send_focused_toplevel();
    } else if(strcmp(what, "focused_layer") == 0) {
        array_push(&server.ipc.watching_layer, fd);
        ipc_send_focused_layer();
    } else {
        write(fd, "null", strlen("null"));
        close(fd);
    }
}

static void
handle_action(int fd, char *action, char **args, size_t arg_count) {
    if(strcmp(action, "exit") == 0) {
        keybind_stop_server(NULL);
    } else if(strcmp(action, "run") == 0) {
        if(arg_count < 1)
            goto done;

        keybind_run(args[0]);
    } else if(strcmp(action, "close") == 0) {
        keybind_close(NULL);
    } else if(strcmp(action, "toggle_floating") == 0) {
        keybind_toggle_floating(NULL);
    } else if(strcmp(action, "move_focus") == 0) {
        if(arg_count < 1)
            goto done;

        enum direction direction;
        if(strcmp(args[0], "up") == 0) {
            direction = DIRECTION_UP;
        } else if(strcmp(args[0], "left") == 0) {
            direction = DIRECTION_LEFT;
        } else if(strcmp(args[0], "down") == 0) {
            direction = DIRECTION_DOWN;
        } else if(strcmp(args[0], "right") == 0) {
            direction = DIRECTION_RIGHT;
        } else {
            goto done;
        }

        keybind_move_focus((void *)direction);
    } else if(strcmp(action, "move") == 0) {
        if(arg_count < 1)
            goto done;

        enum direction direction;
        if(strcmp(args[0], "up") == 0) {
            direction = DIRECTION_UP;
        } else if(strcmp(args[0], "left") == 0) {
            direction = DIRECTION_LEFT;
        } else if(strcmp(args[0], "down") == 0) {
            direction = DIRECTION_DOWN;
        } else if(strcmp(args[0], "right") == 0) {
            direction = DIRECTION_RIGHT;
        } else {
            goto done;
        }

        keybind_move((void *)direction);
    } else if(strcmp(action, "workspace") == 0) {
        if(arg_count < 1)
            goto done;

        keybind_change_workspace((void *)(uintptr_t)atoi(args[0]));
    } else if(strcmp(action, "move_to_workspace") == 0) {
        if(arg_count < 1)
            goto done;

        keybind_move_to_workspace((void *)(uintptr_t)atoi(args[0]));
    } else if(strcmp(action, "next_workspace") == 0) {
        keybind_next_workspace(NULL);
    } else if(strcmp(action, "prev_workspace") == 0) {
        keybind_prev_workspace(NULL);
    } else if(strcmp(action, "toggle_fullscreen") == 0) {
        keybind_toggle_fullscreen(NULL);
    } else if(strcmp(action, "increase_master_ratio") == 0) {
        if(arg_count < 1)
            goto done;

        keybind_increase_master_ratio((void *)(uintptr_t)(atof(args[0]) * 100));
    } else if(strcmp(action, "decrease_master_ratio") == 0) {
        if(arg_count < 1)
            goto done;

        keybind_decrease_master_ratio((void *)(uintptr_t)(atof(args[0]) * 100));
    }

done:
    close(fd);
}

static void
handle_request(int fd, char *buffer) {
    char **words = parser_into_words(buffer, 0);

    for(size_t i = 0; i < array_len(words); i++) {
        wlr_log(WLR_ERROR, "%zu: %s", i, words[i]);
    }

    if(array_len(words) < 2) {
        close(fd);
    } else if(strcmp(words[0], "get") == 0) {
        handle_get(fd, words[1]);
    } else if(strcmp(words[0], "watch") == 0) {
        handle_watch(fd, words[1]);
    } else if(strcmp(words[0], "action") == 0) {
        handle_action(fd, words[1], &words[2], array_len(words) - 2);
    } else {
        close(fd);
    }

    array_destroy(words);
}

static int
ipc_callback(int fd, uint32_t mask, void *data) {
    if((mask & WL_EVENT_ERROR) || (mask & WL_EVENT_HANGUP)) {
        wlr_log(WLR_ERROR, "ipc: error occurred on the socket, quitting");
        ipc_deinit();
        return 0;
    }

    struct sockaddr_un client_address;
    socklen_t sock_len;
    int client_fd = accept(fd, (struct sockaddr *)&client_address, &sock_len);
    if(client_fd < 0) {
        wlr_log(WLR_ERROR, "ipc: failed to accept client");
        return 0;
    }

    char buffer[IPC_MESSAGE_LEN];
    // this is not ideal since it may block for long, it would be better to add this into the loop as another source,
    // but that would need more syncing, which i am lazy to do rn. anyway, if the ipc is used responsibly (throught
    // `mwc-ipc`), this is not a problem
    ssize_t len = read(client_fd, buffer, sizeof(buffer) - 1);
    if(len < 0) {
        wlr_log(WLR_ERROR, "ipc: failed read on fd `%d`", client_fd);
        return 0;
    }

    buffer[len] = 0;
    handle_request(client_fd, buffer);

    return 0;
}

void
ipc_init(void) {
    server.ipc.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(server.ipc.fd < 0) {
        wlr_log(WLR_ERROR, "ipc: failed to open a socket: %s", strerror(errno));
        return;
    }

    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, IPC_SOCKET);

    if(bind(server.ipc.fd, (struct sockaddr *)&address, sizeof(address))) {
        wlr_log(WLR_ERROR, "ipc: failed to bind to socket: %s", strerror(errno));
        close(server.ipc.fd);
        return;
    }

    if(listen(server.ipc.fd, 128) < 0) {
        wlr_log(WLR_ERROR, "ipc: failed to listen on socket: %s", strerror(errno));
        close(server.ipc.fd);
        unlink(IPC_SOCKET);
        return;
    }

    array_init(&server.ipc.watching_workspace);
    array_init(&server.ipc.watching_toplevel);
    array_init(&server.ipc.watching_layer);

    server.ipc.source = wl_event_loop_add_fd(server.event_loop, server.ipc.fd,
            WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR, ipc_callback, NULL);
}

void
ipc_deinit(void) {
    wl_event_source_remove(server.ipc.source);
    server.ipc.source = NULL;

    for(int *iter = server.ipc.watching_workspace; iter <= array_last(server.ipc.watching_workspace); iter++) {
        close(*iter);
    }
    for(int *iter = server.ipc.watching_toplevel; iter <= array_last(server.ipc.watching_toplevel); iter++) {
        close(*iter);
    }
    for(int *iter = server.ipc.watching_layer; iter <= array_last(server.ipc.watching_layer); iter++) {
        close(*iter);
    }

    array_destroy(server.ipc.watching_workspace);
    array_destroy(server.ipc.watching_toplevel);
    array_destroy(server.ipc.watching_layer);

    close(server.ipc.fd);
    unlink(IPC_SOCKET);
}

bool
ipc_running(void) {
    return server.ipc.source != NULL;
}

void
ipc_send_active_workspace(void) {
    if(!ipc_running())
        return;

    struct json_object *workspace = workspace_json(server.active_workspace);

    size_t len;
    const char *stringified = json_object_to_json_string_length(workspace, JSON_C_TO_STRING_PRETTY, &len);

    for(int *iter = server.ipc.watching_workspace; iter <= array_last(server.ipc.watching_workspace); iter++) {
        if(write(*iter, stringified, len) < 0) {
            close(*iter);
            array_remove_by_ptr(&server.ipc.watching_workspace, iter);
            iter--;
        }
    }

    json_object_put(workspace);
}

void
ipc_send_focused_toplevel(void) {
    if(!ipc_running())
        return;

    if(server.focused_toplevel == NULL) {
        for(int *iter = server.ipc.watching_toplevel; iter <= array_last(server.ipc.watching_toplevel); iter++) {
            if(write(*iter, "null", strlen("null")) < 0) {
                close(*iter);
                array_remove_by_ptr(&server.ipc.watching_toplevel, iter);
                iter--;
            }
        }

        return;
    }

    struct json_object *toplevel = toplevel_json(server.focused_toplevel);

    size_t len;
    const char *stringified = json_object_to_json_string_length(toplevel, JSON_C_TO_STRING_PRETTY, &len);

    for(int *iter = server.ipc.watching_toplevel; iter <= array_last(server.ipc.watching_toplevel); iter++) {
        if(write(*iter, stringified, len) < 0) {
            close(*iter);
            array_remove_by_ptr(&server.ipc.watching_toplevel, iter);
            iter--;
        }
    }

    json_object_put(toplevel);
}

void
ipc_send_focused_layer(void) {
    if(!ipc_running())
        return;

    if(server.focused_layer_surface == NULL) {
        for(int *iter = server.ipc.watching_layer; iter <= array_last(server.ipc.watching_layer); iter++) {
            if(write(*iter, "null", strlen("null")) < 0) {
                close(*iter);
                array_remove_by_ptr(&server.ipc.watching_layer, iter);
                iter--;
            }
        }

        return;
    }

    struct json_object *layer = layer_json(server.focused_layer_surface);

    size_t len;
    const char *stringified = json_object_to_json_string_length(layer, JSON_C_TO_STRING_PRETTY, &len);

    for(int *iter = server.ipc.watching_layer; iter <= array_last(server.ipc.watching_layer); iter++) {
        if(write(*iter, stringified, len) < 0) {
            close(*iter);
            array_remove_by_ptr(&server.ipc.watching_layer, iter);
            iter--;
        }
    }

    json_object_put(layer);
}
