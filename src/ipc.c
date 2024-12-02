#include "ipc.h"

#include "owl.h"
#include "toplevel.h"
#include "workspace.h"

#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "wlr/util/log.h"

#define SEPARATOR "\x1E"

extern struct owl_server server;

/* global state that keeps track of connected clients */
static struct wl_list clients;

static void sigpipe_handler(int signum) {
  /* dont exit if broken pipe */
}

static void ipc_create_message(enum ipc_event event, char *buffer, uint32_t length) {
  /* TODO: it would be a good idea to add a server mutex
   * in order to prevent possible race conditions */
  switch(event) {
    case IPC_ACTIVE_WORKSPACE: {
      snprintf(buffer, length, "active-workspace" SEPARATOR "%u" SEPARATOR "%s" SEPARATOR "\n",
               server.active_workspace->index, server.active_workspace->output->wlr_output->name);
      break;
    }
    case IPC_ACTIVE_TOPLEVEL: {
      if(server.focused_toplevel == NULL) {
        snprintf(buffer, length,
                 "active-toplevel" SEPARATOR "" SEPARATOR "" SEPARATOR "\n");
      } else {
        snprintf(buffer, length,
                 "active-toplevel" SEPARATOR "%s" SEPARATOR "%s" SEPARATOR "\n",
                 server.focused_toplevel->xdg_toplevel->app_id,
                 server.focused_toplevel->xdg_toplevel->title);
      }
      break;
    }
  }
}

void ipc_broadcast_message(enum ipc_event event) {
  char message[512];
  ipc_create_message(event, message, sizeof(message));

  /* snprintf doesnt terminate the string if too large, so we do it manually */
  message[sizeof(message) - 1] = 0;

  struct ipc_client *c, *t;
  wl_list_for_each_safe(c, t, &clients, link) {
    if(write(c->fd, message, strlen(message)) == -1) {
      /* this should only fail because of a broken pipe,
       * so we assume the client has been closed */
      wlr_log(WLR_ERROR, "failed to write to a pipe %s, assuming closed", c->name);
      wl_list_remove(&c->link);
      close(c->fd);
      remove(c->name);
      free(c);
    }
  }
}

void *run_ipc(void *args) {
  struct sigaction sa;
  sa.sa_handler = sigpipe_handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);

  if(sigaction(SIGPIPE, &sa, NULL) == -1) {
    wlr_log(WLR_ERROR, "error setting up sigpipe handler");
    return NULL;
  }

  wlr_log(WLR_INFO, "starting owl ipc...");

  remove(PIPE_NAME);
  if(mkfifo(PIPE_NAME, 0622) == -1) {
    wlr_log(WLR_ERROR, "failed to create a pipe");
    return NULL;
  }

  wl_list_init(&clients);

  int fifo_fd = open(PIPE_NAME, O_RDONLY);
  if(fifo_fd == -1) {
    wlr_log(WLR_ERROR, "failed to open a pipe");
    goto clean;
  }

  /* large enough buffer so the probabality of cutting a message short is small */
  char buffer[512];

  int bytes_read;
  while(true) {
    bytes_read = read(fifo_fd, buffer, sizeof(buffer) - 1);
    if(bytes_read == -1) {
      wlr_log(WLR_ERROR, "failed read from the pipe");
      goto clean;
    }

    if(bytes_read == 0) {
      usleep(100000);
      continue;
    }

    /* preventing overflow */
    buffer[bytes_read] = 0;
    
    /* we sleep a bit so clients can create their pipes */
    usleep(100000);

    char name[512];
    char *q = name;
    for(size_t i = 0; i < bytes_read; i++) {
      if(buffer[i] == '\n') {
        *q = 0;

        int client_pipe_fd = open(name, O_WRONLY | O_NONBLOCK);
        if(client_pipe_fd == -1) {
          wlr_log(WLR_ERROR, "failed to open clients pipe");
          continue;
        }

        wlr_log(WLR_INFO, "new ipc client subscribed on pipe '%s'", name);

        struct ipc_client *c = calloc(1, sizeof(*c));
        c->fd = client_pipe_fd;
        strncpy(c->name, name, MAX_CLIENT_PIPE_NAME_LENGTH);
        /* terminate the string */
        c->name[sizeof(c->name) - 1] = 0;
        wl_list_insert(&clients, &c->link);

        ipc_broadcast_message(IPC_ACTIVE_TOPLEVEL);
        ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);
        q = name;
      } else {
        *q = buffer[i];
        q++;
      }
    }

  }

clean:
  close(fifo_fd);
  return NULL;
}

