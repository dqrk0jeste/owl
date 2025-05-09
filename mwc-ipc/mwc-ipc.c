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

static void
action(char **args, size_t arg_count) {
    char buffer[IPC_MESSAGE_LEN] = "action ";
    for(size_t i = 0; i < arg_count; i++) {
        strncat(buffer, args[i], sizeof(buffer) - strlen(buffer) - 1);
        strncat(buffer, " ", sizeof(buffer) - strlen(buffer) - 1);
    }

    if(write(server_fd, buffer, strlen(buffer)) < 0) {
        fprintf(stderr, "failed to write the message, is the compositor running?\n");
    };
}

static void
watch(char **args, size_t arg_count) {
    char buffer[IPC_MESSAGE_LEN] = "watch ";
    for(size_t i = 0; i < arg_count; i++) {
        strncat(buffer, args[i], sizeof(buffer) - strlen(buffer) - 1);
        strncat(buffer, " ", sizeof(buffer) - strlen(buffer) - 1);
    }

    if(write(server_fd, buffer, strlen(buffer)) < 0) {
        fprintf(stderr, "failed to write the message, is the compositor running?\n");
        return;
    };

    while(1) {
        ssize_t len = read(server_fd, buffer, sizeof(buffer) - 1);
        if(len <= 0)
            return;

        buffer[len] = 0;
        printf("%s", buffer);
        fflush(stdout);
    }
}

static void
get(char **args, size_t arg_count) {
    char buffer[IPC_MESSAGE_LEN] = "get ";
    for(size_t i = 0; i < arg_count; i++) {
        strncat(buffer, args[i], sizeof(buffer) - strlen(buffer) - 1);
        strncat(buffer, " ", sizeof(buffer) - strlen(buffer) - 1);
    }

    if(write(server_fd, buffer, strlen(buffer)) < 0) {
        fprintf(stderr, "failed to write the message, is the compositor running?\n");
        return;
    };

    ssize_t len = read(server_fd, buffer, sizeof(buffer) - 1);
    if(len <= 0)
        return;

    buffer[len] = 0;
    printf("%s", buffer);
    fflush(stdout);
}

bool
send_request(char *predicate, char **args, size_t arg_count) {
    if(strcmp(predicate, "get") == 0) {
        get(args, arg_count);
    } else if(strcmp(predicate, "watch") == 0) {
        watch(args, arg_count);
    } else if(strcmp(predicate, "action") == 0) {
        action(args, arg_count);
    } else {
        fprintf(stderr, "invalid predicate: %s", predicate);
        return false;
    }

    return true;
}

int
main(int argc, char *argv[]) {
    if(argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("usage: mwc-ipc <predicate>\n\n"
               "where `predicate` is one of\n"
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
    send_request(argv[1], &argv[2], argc - 2);

    close(server_fd);

    return 0;
}
