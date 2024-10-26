#include "ipc.h"

extern struct owl_server server;

/* global state that keeps track of connected clients */
struct wl_list clients;

static void sigpipe_handler(int signum) {
  /* dont panic if broken pipe */
}

static void ipc_create_message(enum ipc_event event, char *buffer, uint32_t length) {
  /* TODO: it would be a good idea to add a server mutex
   * in order to prevent possible race conditions */
  switch(event) {
    case ACTIVE_WORKSPACE: {
      snprintf(buffer, length, "active_workspace %d %s",
        server.active_workspace->index, server.active_workspace->output->wlr_output->name);
      break;
    }
    case ACTIVE_TOPLEVEL: {
      snprintf(buffer, length, "active_toplevel %s %s",
        server.focused_toplevel->xdg_toplevel->app_id,
        server.focused_toplevel->xdg_toplevel->title);
      break;
    }
  }
}

void ipc_broadcast_message(enum ipc_event event) {
  wlr_log(WLR_ERROR, "1");
  char message[512];
  ipc_create_message(event, message, sizeof(message));
  wlr_log(WLR_ERROR, "2");

  struct ipc_client *c, *t;
  wl_list_for_each_safe(c, t, &clients, link) {
    wlr_log(WLR_INFO, "writing the message to the client '%s'\n", c->name);
    if(write(c->fd, message, strlen(message) + 1) == -1) {
      /* this should only fail because of a broken pipe,
       * so we assume the client has been closed */
      wlr_log(WLR_ERROR, "failed to write to a pipe %s\n", c->name);
      wl_list_remove(&c->link);
      free(c);
      continue;
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

  wlr_log(WLR_INFO, "starting ipc...\n");

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
 
  char client_pipe_name[128];

  int bytes_read;
  while(true) {
    bytes_read = read(fifo_fd, client_pipe_name, sizeof(client_pipe_name) - 1);
    if(bytes_read == -1) {
      wlr_log(WLR_ERROR, "failed read from the pipe");
      goto clean;
    }

    if(bytes_read == 0 || client_pipe_name[0] == 0) continue;

    /* preventing overflow */
    client_pipe_name[bytes_read] = 0;

    wlr_log(WLR_INFO, "new client subscribed on pipe '%s'\n", client_pipe_name);

    if(mkfifo(client_pipe_name, 0644) == -1) {
      wlr_log(WLR_ERROR, "failed to create a pipe");
      continue;
    }

    int client_pipe_fd = open(client_pipe_name, O_WRONLY);
    if(client_pipe_fd == -1) {
      wlr_log(WLR_ERROR, "failed to open clients pipe");
      close(client_pipe_fd);
      continue;
    }

    struct ipc_client *c = calloc(1, sizeof(*c));
    c->fd = client_pipe_fd;
    strncpy(c->name, client_pipe_name, MAX_CLIENT_PIPE_NAME_LENGTH);

    wl_list_insert(&clients, &c->link);
  }

clean:
  close(fifo_fd);
  return NULL;
}

