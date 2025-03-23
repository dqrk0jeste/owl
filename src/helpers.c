#include "helpers.h"

#include "config.h"

void
run_cmd(char *cmd) {
  if(fork() == 0) {
    execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
  }
}

int
box_area(struct wlr_box *box) {
  return box->width * box->height;
}

void
mwc_color_to_wlr_color(struct mwc_color color, float dest[static 4]) {
  dest[0] = color.r / 255.0;
  dest[1] = color.g / 255.0;
  dest[2] = color.b / 255.0;
  dest[3] = color.a / 255.0;
}

void
mwc_color_to_pixman_color(struct mwc_color color, pixman_color_t *dest) {
  dest->red = color.r * 257;
  dest->green = color.g * 257;
  dest->blue = color.b * 257;
  dest->alpha = color.a * 257;
}
