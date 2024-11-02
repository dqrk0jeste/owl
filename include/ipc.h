#ifndef IPC_H
#define IPC_H

#include <signal.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <wayland-util.h>

#include "owl.h"
#include "wlr/util/log.h"

#define PIPE_NAME "/tmp/owl"
#define MAX_CLIENTS 128
#define MAX_CLIENT_PIPE_NAME_LENGTH 128

struct ipc_client {
  char name[MAX_CLIENT_PIPE_NAME_LENGTH];
  int fd;
  struct wl_list link;
};

enum ipc_event {
  IPC_ACTIVE_WORKSPACE,
  IPC_ACTIVE_TOPLEVEL,
};

void ipc_broadcast_message(enum ipc_event event);

void *run_ipc(void *args);
#endif /* ifndef IPC_H */
