#include "ipc.h"

#include <assert.h>
#include <pthread.h>
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

extern struct server server;

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
    if(!ipc_running())
        return;

    char message[512];
    ipc_create_message(event, message, sizeof(message));

    for(size_t i = 0; i < array_len(server.ipc.client_fds); i++) {
        if(write(server.ipc.client_fds[i], message, strlen(message)) < 0) {
            wlr_log(WLR_INFO, "could not write to client %d, assuming closed", server.ipc.client_fds[i]);
            close(server.ipc.client_fds[i]);
            array_remove(&server.ipc.client_fds, i);
            i--;
        }
    }
}

void
ipc_add_client(int fd) {
    array_push(&server.ipc.client_fds, fd);
    for(size_t i = 0; i < IPC_EVENT_COUNT; i++) {
        ipc_broadcast_message(i);
    }
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

    // wlr_log(WLR_INFO, "ipc: new client on fd %d", client_fd);

    char buffer[IPC_MESSAGE_LEN];
    ssize_t len = read(client_fd, buffer, sizeof(buffer) - 1);
    if(len < 0) {
        wlr_log(WLR_ERROR, "ipc: failed read on fd %d", client_fd);
        return 0;
    }

    buffer[len] = 0;

    // handle request here
    wlr_log(WLR_INFO, "ipc: new client message %s", buffer);

    // if(strcmp(buffer, "subscribe") == 0) {
    //     ipc_add_client(client);
    // } else {
    //     ipc_handle_simple(buffer, client);
    // }

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

    array_init(&server.ipc.client_fds);
    server.ipc.source = wl_event_loop_add_fd(server.wl_event_loop, server.ipc.fd,
            WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR, ipc_callback, NULL);
}

void
ipc_deinit(void) {
    wl_event_source_remove(server.ipc.source);
    server.ipc.source = NULL;
    close(server.ipc.fd);

    // go backwards so the memory is not moved a lot
    for(ssize_t i = array_len(server.ipc.client_fds); i >= 0; i--) {
        close(server.ipc.client_fds[i]);
        array_remove(&server.ipc.client_fds, i);
    }

    unlink(IPC_SOCKET);
}

bool
ipc_running(void) {
    return server.ipc.source != NULL;
}
