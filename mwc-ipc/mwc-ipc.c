#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ipc_shared.h"

// action - keybind dispatchers
// get - get some state
// watch - watch for some state change

int server_fd;

void
ipc_send(char *message, bool wait_response) {
}

void
ipc_subscribe(int fd) {
    if(write(fd, "subscribe", strlen("subscribe")) < 0) {
        printf("failed to write the message, is the compositor running?\n");
        return;
    };

    char buffer[IPC_MESSAGE_LEN];
    while(1) {
        ssize_t len = read(fd, buffer, sizeof(buffer) - 1);
        if(len <= 0)
            return;

        buffer[len] = 0;
        printf("%s", buffer);
        fflush(stdout);
    }
}

void
ipc_simple(int fd, char *message) {
    if(write(fd, message, strlen(message)) < 0) {
        printf("failed to write the message, is the compositor running?\n");
        return;
    };

    char buffer[IPC_MESSAGE_LEN];
    ssize_t len = read(fd, buffer, sizeof(buffer) - 1);
    if(len <= 0)
        return;

    buffer[len] = 0;
    printf("%s", buffer);
    fflush(stdout);
}

int
main(int argc, char *argv[]) {
    if(argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("usage: mwc-ipc message\n\n"
               "where message is one of\n"
               "\tget <toplevels|outputs|layers|workspaces|active_workspace|focused_toplevel|focused_layer> - get the "
               "information about the asked resourse\n"
               "\twatch <active_workspace|focused_toplevel|focused_layer> - watch the specified state\n"
               "\action <action> - perform the desired action\n");
        return 0;
    }

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(server_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, IPC_SOCKET);

    if(connect(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("connect");
        close(server_fd);
        return 1;
    }

    // code here

    close(server_fd);

    return 0;
}
