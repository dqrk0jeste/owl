#include "helpers.h"

#include <assert.h>
#include <time.h>

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

struct wl_list *
list_at(struct wl_list *list, size_t index) {
    size_t i = 0;
    struct wl_list *iter = list->next;
    while(i < index) {
        if(iter == list->prev)
            return NULL;
        iter = iter->next;
        i++;
    }

    return iter;
}

ssize_t
list_index_of(struct wl_list *list, struct wl_list *elem) {
    ssize_t i = 0;
    struct wl_list *iter = list->next;
    while(iter != list) {
        if(iter == elem)
            return i;
        iter = iter->next;
        i++;
    }

    return -1;
}

enum direction
opposite(enum direction direction) {
    switch(direction) {
        case DIRECTION_UP:
            return DIRECTION_DOWN;
        case DIRECTION_RIGHT:
            return DIRECTION_LEFT;
        case DIRECTION_DOWN:
            return DIRECTION_UP;
        case DIRECTION_LEFT:
            return DIRECTION_RIGHT;
    }
}

void
color_to_wlr_color(struct color color, float dest[static 4]) {
    dest[0] = color.r / 255.0;
    dest[1] = color.g / 255.0;
    dest[2] = color.b / 255.0;
    dest[3] = color.a / 255.0;
}

void
color_to_pixman_color(struct color color, pixman_color_t *dest) {
    dest->red = color.r * 257;
    dest->green = color.g * 257;
    dest->blue = color.b * 257;
    dest->alpha = color.a * 257;
}

uint32_t
timespec_to_ms(struct timespec *ts) {
    return (uint32_t)ts->tv_sec * 1000 + (uint32_t)ts->tv_nsec / 1000000;
}

uint32_t
get_now_in_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return timespec_to_ms(&now);
}

bool
matches_relation(enum relation relation, int a, int b) {
    switch(relation) {
        case RELATION_EQUAL:
            return a == b;
        case RELATION_GREATER_THAN:
            return a > b;
        case RELATION_SMALLER_THAN:
            return a < b;
    }

    assert(false && "unreachable");
    return false;
}

void
color_premultiply(struct color *color) {
    color->r *= color->a / 255.0;
    color->g *= color->a / 255.0;
    color->b *= color->a / 255.0;
}

void
box_midpoint(const struct wlr_box *box, int *x, int *y) {
    *x = box->x + box->width / 2;
    *y = box->y + box->height / 2;
}

void
get_same_relative_coords(int *x, int *y, const struct wlr_box *old, const struct wlr_box *new) {
    double relative_x = (double)(*x - old->x) / old->width;
    double relative_y = (double)(*y - old->y) / old->height;

    *x = new->x + relative_x *new->width;
    *y = new->y + relative_y *new->height;
}

struct wlr_box
create_centered_box_for_box(struct wlr_box *box, int width, int height) {
    return (struct wlr_box){
            .x = box->x + (box->width - width) / 2,
            .y = box->y + (box->height - height) / 2,
            .width = width,
            .height = height,
    };
}
