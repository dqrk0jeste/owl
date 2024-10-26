/* you are suppossed to use this client implementation (installed globally as `owl-ipc`)
 * to get ipc messages from the server. see examples/current-workspace.sh */
#include <assert.h>
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

const char letters[] = "abcdefghijklmnopqrstuvwxyz";

void generate_random_name(char *buffer, uint32_t length, uint32_t buffer_size) {
  assert(buffer_size >= 5 + length + 1);

  strcpy(buffer, "/tmp/");
  for(size_t i = 0; i < length; i++) {
    buffer[5 + i] = letters[rand() % (sizeof(letters) - 1)];
  }
  buffer[5 + length] = 0;
}

int main(int argc, char **argv) {
  srand(time(NULL));

  int owl_fd = open(OWL_PIPE, O_WRONLY);
  if(owl_fd == -1) {
    perror("failed to open fifo");
    return 1;
  }

  char name[128];
  generate_random_name(name, 6, sizeof(name));

  if(write(owl_fd, name, sizeof(name)) == -1) {
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

  printf("successfully created a connection over pipe '%s'\n"
         "waiting for events...\n", name);

  char buffer[128];
  int bytes_read;
  uint32_t i = 1;
  while(true) {
    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if(bytes_read == -1) {
      perror("failed to read from the pipe");
      goto clean;
    }

    if(bytes_read == 0) continue;

    /* preventing overflow */
    buffer[bytes_read] = 0;

    printf("%d. received message: %s\n", i, buffer);
    i++;
  }

clean:
  printf("closing...\n");
  close(fd);
  return 1;
}
