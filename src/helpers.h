#pragma once

#include <unistd.h>
#include <wlr/util/box.h>

/* these next few functions are some helpers,
 * implemented straight in this header file */

struct vec2 {
  double x, y;
};

static void
run_cmd(char *cmd) {
  if(fork() == 0) {
    execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
  }
}

static int
box_area(struct wlr_box *box) {
  return box->width * box->height;
}
