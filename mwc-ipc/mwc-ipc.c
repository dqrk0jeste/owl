#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ipc_shared.h"

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
        fprintf(stderr,
                "usage: mwc-ipc message\n"
                "where message is one of\n"
                "\tsubscribe - receive all the events from the compositor\n"
                "\ttoplevels - list app_ids and titles of all the toplevels\n"
                "\tlayers - list namespaces of all the layers\n"
                "\toutputs - list names of all the outputs\n");
        return 0;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, IPC_PATH);

    if(connect(fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("connect");
        close(fd);
        return 1;
    }

    if(strcmp(argv[1], "subscribe") == 0) {
        ipc_subscribe(fd);
    } else {
        ipc_simple(fd, argv[1]);
    }

    close(fd);

    return 0;
}
