#pragma once

#include <pixman.h>
#include <stdint.h>
#include <unistd.h>
#include <wlr/util/box.h>

#define clamp(v, a, b) (max((a), min((v), (b))))

struct color {
    uint8_t r, g, b, a;
};

void
run_cmd(char *cmd);

int
box_area(struct wlr_box *box);

struct wl_list *
list_at(struct wl_list *list, size_t index);

void
color_to_wlr_color(struct color color, float dest[static 4]);

void
color_to_pixman_color(struct color color, pixman_color_t *dest);

uint32_t
timespec_to_ms(struct timespec *ts);

uint32_t
get_now_in_ms(void);
