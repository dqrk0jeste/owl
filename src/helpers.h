#pragma once

#include <pixman.h>
#include <stdint.h>
#include <unistd.h>
#include <wlr/util/box.h>

#define clamp(v, a, b) (max((a), min((v), (b))))

struct mwc_color {
    uint8_t r, g, b, a;
};

void
run_cmd(char *cmd);

int
box_area(struct wlr_box *box);

void
mwc_color_to_wlr_color(struct mwc_color color, float dest[static 4]);

void
mwc_color_to_pixman_color(struct mwc_color color, pixman_color_t *dest);

uint32_t
timespec_to_ms(struct timespec *ts);
