#pragma once

/* here is the ipc protocol implemented in this file(server) and owl-ipc(client):
 *  - there is a pipe called PIPE_NAME opened by the server
 *  - clients who want to subscribe to the ipc to receive events should open another pipe,
 *    and then send the new pipes name to PIPE_NAME and add \x1E to the end to signalize end of message
 *  - server will then open the new pipe and start sending down all the events
 *  - if a client wants to stop receiving events it just needs to close the file descriptor */

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <wayland-util.h>

#define PIPE_NAME "/tmp/owl/ipc"
#define MAX_CLIENT_PIPE_NAME_LENGTH 128

struct ipc_client {
  char name[MAX_CLIENT_PIPE_NAME_LENGTH];
  int fd;
  struct wl_list link;
};

enum ipc_event {
  IPC_ACTIVE_WORKSPACE,
  IPC_ACTIVE_TOPLEVEL,
  IPC_EVENT_COUNT,
};

void ipc_broadcast_message(enum ipc_event event);

void *run_ipc(void *args);
