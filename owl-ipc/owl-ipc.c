/* you are suppossed to use this client implementation (installed globally as `owl-ipc`)
 * to get ipc messages from the server. see examples/active-workspace.sh */
#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define OWL_PIPE "/tmp/owl"

static bool interupted = false;
const char letters[] = "abcdefghijklmnopqrstuvwxyz";

static void sigint_handler(int signum) {
  /* dont panic if broken pipe */
  interupted = true;
}

void generate_random_name(char *buffer, uint32_t length, uint32_t buffer_size) {
  assert(buffer_size >= 5 + length + 1);

  strcpy(buffer, "/tmp/");
  for(size_t i = 0; i < length; i++) {
    buffer[5 + i] = letters[rand() % (sizeof(letters) - 1)];
  }
  buffer[5 + length] = 0;
}

int main(int argc, char **argv) {
  struct sigaction sa;
  sa.sa_handler = sigint_handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);

  if(sigaction(SIGINT, &sa, NULL) == -1) {
    perror("error setting up sigint handler");
    return 1;
  }

  /* using time(0) here caused really weird behaviour when multiple instances were run,
   * and caused hours of debugging */
  srand(getpid());

  int owl_fd = open(OWL_PIPE, O_WRONLY);
  if(owl_fd == -1) {
    perror("failed to open pipe /tmp/owl");
    return 1;
  }

  char name[128];
  generate_random_name(name, 6, sizeof(name));

  if(write(owl_fd, name, strlen(name)) == -1) {
    perror("failed to write to fifo");
    return 1;
  }

  /* we wont use this pipe anymore */
  close(owl_fd);

  /* wait for the server to open the requested pipe */
  sleep(1);

  int fd = open(name, O_RDONLY);
  if(fd == -1) {
    perror("failed to open pipe");
    goto clean;
  }

  /*printf("successfully created a connection over pipe '%s'\n"*/
  /*       "waiting for events...\n", name);*/

  char buffer[128];
  int bytes_read;
  while(!interupted) {
    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if(bytes_read == -1) {
      perror("failed to read from the pipe");
      goto clean;
    }

    if(bytes_read == 0) {
      usleep(100000);
      continue;
    }

    /* preventing overflow */
    buffer[bytes_read] = 0;

    char message[512];
    char *q = message;
    for(size_t i = 0; i < bytes_read; i++) {
      if(buffer[i] == '\n') {
        *q = 0;
        printf("%s\n", message);
        fflush(stdout);
        q = message;
      } else {
        *q = buffer[i];
        q++;
      }
    }
  }

clean:
  printf("closing...\n");
  close(fd);
  return !interupted;
}
