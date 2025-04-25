#include "helpers.h"

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
