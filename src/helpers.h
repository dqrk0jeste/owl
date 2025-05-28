#pragma once

#include <pixman.h>
#include <stdint.h>
#include <unistd.h>
#include <wlr/util/box.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define clamp(v, a, b) (max((a), min((v), (b))))

#define STRING_INITIAL_LENGTH 64

enum direction {
    DIRECTION_UP = 0,
    DIRECTION_RIGHT,
    DIRECTION_DOWN,
    DIRECTION_LEFT,
};

enum relation {
    RELATION_EQUAL,
    RELATION_GREATER_THAN,
    RELATION_SMALLER_THAN,
};

struct color {
    uint8_t r, g, b, a;
};

void
run_cmd(char *cmd);

int
box_area(struct wlr_box *box);

struct wl_list *
list_at(struct wl_list *list, size_t index);

ssize_t
list_index_of(struct wl_list *list, struct wl_list *elem);

enum direction
opposite(enum direction direction);

void
color_to_wlr_color(struct color color, float dest[static 4]);

void
color_to_pixman_color(struct color color, pixman_color_t *dest);

uint32_t
timespec_to_ms(struct timespec *ts);

uint32_t
get_now_in_ms(void);

bool
matches_relation(enum relation relation, int a, int b);

void
color_premultiply(struct color *color);

void
box_midpoint(const struct wlr_box *box, int *x, int *y);

void
get_same_relative_coords(int *x, int *y, const struct wlr_box *old, const struct wlr_box *new);

struct wlr_box
create_centered_box_for_box(struct wlr_box *box, int width, int height);
