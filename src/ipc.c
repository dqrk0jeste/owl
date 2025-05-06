#include "ipc.h"

#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "array.h"
#include "layer_surface.h"
#include "mwc.h"
#include "output.h"
#include "workspace.h"

// this part of the code is horrendous, and in a a desparate need of a rewrite
// will probably use json format in the future

extern struct server server;

void
sigpipe_handler(int signum) {
    // do nothing
}

void
ipc_create_message(enum ipc_event event, char *buffer, uint32_t length) {
    switch(event) {
        case IPC_ACTIVE_WORKSPACE: {
            snprintf(buffer, length, "active-workspace" SEPARATOR "%u" SEPARATOR "%s" SEPARATOR "\n",
                    server.active_workspace->index, server.active_workspace->output->wlr_output->name);
            break;
        }
        case IPC_ACTIVE_TOPLEVEL: {
            if(server.focused_toplevel == NULL) {
                snprintf(buffer, length, "active-toplevel" SEPARATOR "" SEPARATOR "" SEPARATOR "\n");
            } else {
                snprintf(buffer, length, "active-toplevel" SEPARATOR "%s" SEPARATOR "%s" SEPARATOR "\n",
                        server.focused_toplevel->xdg_toplevel->app_id, server.focused_toplevel->xdg_toplevel->title);
            }
            break;
        }
        case IPC_EVENT_COUNT: {
            assert(false && "you should not have done this");
        }
    }
}

void
ipc_broadcast_message(enum ipc_event event) {
    if(!server.ipc_running)
        return;

    char message[512];
    ipc_create_message(event, message, sizeof(message));

    for(size_t i = 0; i < array_len(server.ipc_clients); i++) {
        if(write(server.ipc_clients[i], message, strlen(message)) < 0) {
            wlr_log(WLR_INFO, "could not write to client %d, assuming closed", server.ipc_clients[i]);
            close(server.ipc_clients[i]);
            array_remove(&server.ipc_clients, i);
            i--;
        }
    }
}

void
ipc_add_client(int fd) {
    array_push(&server.ipc_clients, fd);
    for(size_t i = 0; i < IPC_EVENT_COUNT; i++) {
        ipc_broadcast_message(i);
    }
}

// this is horrendous, but i dont care, never going to touch it again
void
ipc_handle_simple(char *request, int fd) {
    size_t len = 0;
    size_t cap = STRING_INITIAL_LENGTH;
    char *message = calloc(cap, sizeof(char));
    char *p = message;
    if(strcmp(request, "toplevels") == 0) {
        struct output *output;
        wl_list_for_each(output, &server.outputs, link) {
            struct workspace *workspace;
            wl_list_for_each(workspace, &output->workspaces, link) {
                struct toplevel *toplevel;
                wl_list_for_each(toplevel, &workspace->floating, link) {
                    char *q = toplevel->xdg_toplevel->app_id;
                    while(*q != 0) {
                        if(len >= cap) {
                            cap *= 2;
                            message = realloc(message, cap);
                            p = message + len;
                        }
                        *p = *q;
                        p++;
                        q++;
                        len++;
                    }

                    if(len >= cap) {
                        cap *= 2;
                        message = realloc(message, cap);
                        p = message + len;
                    }

                    *p = ',';
                    p++;
                    len++;

                    q = toplevel->xdg_toplevel->title;
                    while(*q != 0) {
                        if(len >= cap) {
                            cap *= 2;
                            message = realloc(message, cap);
                            p = message + len;
                        }
                        *p = *q;
                        p++;
                        q++;
                        len++;
                    }

                    *p = '\n';
                    p++;
                    len++;
                }

                wl_list_for_each(toplevel, &workspace->masters, link) {
                    char *q = toplevel->xdg_toplevel->app_id;
                    while(*q != 0) {
                        if(len >= cap) {
                            cap *= 2;
                            message = realloc(message, cap);
                            p = message + len;
                        }
                        *p = *q;
                        p++;
                        q++;
                        len++;
                    }

                    if(len >= cap) {
                        cap *= 2;
                        message = realloc(message, cap);
                        p = message + len;
                    }

                    *p = ',';
                    p++;
                    len++;

                    q = toplevel->xdg_toplevel->title;
                    while(*q != 0) {
                        if(len >= cap) {
                            cap *= 2;
                            message = realloc(message, cap);
                            p = message + len;
                        }
                        *p = *q;
                        p++;
                        q++;
                        len++;
                    }

                    *p = '\n';
                    p++;
                    len++;
                }

                wl_list_for_each(toplevel, &workspace->slaves, link) {
                    char *q = toplevel->xdg_toplevel->app_id;
                    while(*q != 0) {
                        if(len >= cap) {
                            cap *= 2;
                            message = realloc(message, cap);
                            p = message + len;
                        }
                        *p = *q;
                        p++;
                        q++;
                        len++;
                    }

                    if(len >= cap) {
                        cap *= 2;
                        message = realloc(message, cap);
                        p = message + len;
                    }

                    *p = ',';
                    p++;
                    len++;

                    q = toplevel->xdg_toplevel->title;
                    while(*q != 0) {
                        if(len >= cap) {
                            cap *= 2;
                            message = realloc(message, cap);
                            p = message + len;
                        }
                        *p = *q;
                        p++;
                        q++;
                        len++;
                    }

                    *p = '\n';
                    p++;
                    len++;
                }
            }
        }
    } else if(strcmp(request, "layers") == 0) {
        struct output *output;
        wl_list_for_each(output, &server.outputs, link) {
            struct layer_surface *layer;
            for(size_t i = 0; i < 4; i++) {
                wl_list_for_each(layer, &(&output->layers.background)[i], link) {
                    char *q = layer->wlr_layer_surface->namespace;
                    while(*q != 0) {
                        if(len >= cap) {
                            cap *= 2;
                            message = realloc(message, cap);
                            p = message + len;
                        }
                        *p = *q;
                        p++;
                        q++;
                        len++;
                    }

                    if(len >= cap) {
                        cap *= 2;
                        message = realloc(message, cap);
                        p = message + len;
                    }

                    *p = '\n';
                    p++;
                    len++;
                }
            }
        }
    } else if(strcmp(request, "outputs") == 0) {
        struct output *output;
        wl_list_for_each(output, &server.outputs, link) {
            char *q = output->wlr_output->name;
            while(*q != 0) {
                if(len >= cap) {
                    cap *= 2;
                    message = realloc(message, cap);
                    p = message + len;
                }
                *p = *q;
                p++;
                q++;
                len++;
            }

            if(len >= cap) {
                cap *= 2;
                message = realloc(message, cap);
                p = message + len;
            }

            *p = '\n';
            p++;
            len++;
        }
    } else {
        message = "invalid request\n";
        len = strlen(message);
    }

    write(fd, message, len);
    free(message);
    close(fd);
}

void *
ipc_run(void *data) {
    struct sigaction sa;
    sa.sa_handler = sigpipe_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if(sigaction(SIGPIPE, &sa, NULL) == -1)
        goto no_close;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd == -1)
        goto no_close;

    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, IPC_PATH);

    if(bind(fd, (struct sockaddr *)&address, sizeof(address)))
        goto error;
    if(listen(fd, 128) == -1)
        goto error;

    array_init(&server.ipc_clients);
    server.ipc_running = true;

    char buffer[1024];
    while(1) {
        struct sockaddr_un client_address;
        socklen_t sock_len;
        int client = accept(fd, (struct sockaddr *)&client_address, &sock_len);
        if(client == -1)
            continue;

        wlr_log(WLR_INFO, "ipc: new client on fd: %d", client);

        ssize_t len = read(client, buffer, sizeof(buffer) - 1);
        if(len < 0)
            continue;

        buffer[len] = 0;

        if(strcmp(buffer, "subscribe") == 0) {
            ipc_add_client(client);
        } else {
            ipc_handle_simple(buffer, client);
        }
    }

error:
    wlr_log(WLR_ERROR, "ipc: %s", strerror(errno));
    close(fd);
no_close:
    unlink(IPC_PATH);
    return NULL;
}
