#include "layout.h"

#include <stdint.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>

#include "config.h"
#include "mwc.h"
#include "toplevel.h"
#include "wlr/util/box.h"

extern struct mwc_server server;

void
layout_get_masters_container_size(struct mwc_output *output, uint32_t master_count, uint32_t slave_count,
        uint32_t *width, uint32_t *height) {
    uint32_t outer_gaps = server.config->outer_gaps;
    uint32_t inner_gaps = server.config->inner_gaps;
    double master_ratio = server.config->master_ratio;

    struct wlr_box output_box = output->usable_area;

    uint32_t total_width = slave_count > 0 ? output_box.width * master_ratio : output_box.width;

    uint32_t total_gaps = slave_count > 0 ? outer_gaps  // left outer gaps
                    + (master_count - 1) * 2 * inner_gaps  // inner gaps between masters
                    + inner_gaps  // right inner gaps
                                          : outer_gaps  // left outer gaps
                    + (master_count - 1) * 2 * inner_gaps  // inner gaps between masters
                    + outer_gaps;  // right outer gaps

    *width = (total_width - total_gaps) / master_count;
    *height = output_box.height - 2 * outer_gaps;
}

void
layout_get_slaves_container_size(struct mwc_output *output, uint32_t slave_count, uint32_t *width, uint32_t *height) {
    uint32_t outer_gaps = server.config->outer_gaps;
    uint32_t inner_gaps = server.config->inner_gaps;
    double master_ratio = server.config->master_ratio;

    struct wlr_box output_box = output->usable_area;

    uint32_t total_gaps = outer_gaps  // top outer gaps
            + (slave_count - 1) * 2 * inner_gaps  // inner gaps between slaves
            + outer_gaps;  // bottom outer gaps

    *width = output_box.width * (1 - master_ratio) - outer_gaps - inner_gaps;
    *height = (output_box.height - total_gaps) / slave_count;
}

bool
toplevel_is_master(struct mwc_toplevel *toplevel) {
    struct mwc_toplevel *t;
    wl_list_for_each(t, &toplevel->workspace->masters, link) {
        if(toplevel == t) return true;
    };
    return false;
}

bool
toplevel_is_slave(struct mwc_toplevel *toplevel) {
    struct mwc_toplevel *t;
    wl_list_for_each(t, &toplevel->workspace->slaves, link) {
        if(toplevel == t) return true;
    };
    return false;
}

void
layout_configure(struct mwc_workspace *workspace) {
    // if there is a fullscreened toplevel we just skip
    if(workspace->fullscreen_toplevel != NULL) return;

    // if there are no masters we are done
    if(wl_list_empty(&workspace->masters)) return;

    struct mwc_output *output = workspace->output;

    uint32_t outer_gaps = server.config->outer_gaps;
    uint32_t inner_gaps = server.config->inner_gaps;

    uint32_t slave_count = wl_list_length(&workspace->slaves);
    uint32_t master_count = wl_list_length(&workspace->masters);

    uint32_t width, height;
    layout_get_masters_container_size(output, master_count, slave_count, &width, &height);

    struct wlr_box box = {.width = width, .height = height};

    struct mwc_toplevel *toplevel;
    size_t i = 0;
    wl_list_for_each(toplevel, &workspace->masters, link) {
        box.x = output->usable_area.x + outer_gaps + box.width * i + inner_gaps * 2 * i;
        box.y = output->usable_area.y + outer_gaps;

        toplevel_set_state(toplevel, box);
        i++;
    }

    if(slave_count == 0) return;

    layout_get_slaves_container_size(workspace->output, slave_count, &width, &height);

    box.width = width;
    box.height = height;

    i = 0;
    wl_list_for_each(toplevel, &workspace->slaves, link) {
        box.x = output->usable_area.x + output->usable_area.width * server.config->master_ratio + inner_gaps;
        box.y = output->usable_area.y + outer_gaps + height * i + inner_gaps * 2 * i;

        toplevel_set_state(toplevel, box);
        i++;
    }
}

// this function assumes they are in the same workspace and that t2 comes after t1 if in the same list
void
layout_swap_toplevels(struct mwc_toplevel *t1, struct mwc_toplevel *t2) {
    struct wl_list *before_t1 = t1->link.prev;
    wl_list_remove(&t1->link);
    wl_list_insert(&t2->link, &t1->link);
    wl_list_remove(&t2->link);
    wl_list_insert(before_t1, &t2->link);

    layout_configure(t1->workspace);
}

struct mwc_toplevel *
layout_find_closest_toplevel(struct mwc_workspace *workspace, bool master, enum mwc_direction side) {
    // this means there are no tiled toplevels
    if(wl_list_empty(&workspace->masters)) return NULL;

    struct mwc_toplevel *first_master = wl_container_of(workspace->masters.next, first_master, link);
    struct mwc_toplevel *last_master = wl_container_of(workspace->masters.prev, last_master, link);

    struct mwc_toplevel *first_slave = NULL;
    struct mwc_toplevel *last_slave = NULL;
    if(!wl_list_empty(&workspace->slaves)) {
        first_slave = wl_container_of(workspace->slaves.next, first_slave, link);
        last_slave = wl_container_of(workspace->slaves.prev, last_slave, link);
    }

    switch(side) {
        case MWC_UP: {
            if(master || first_slave == NULL) return first_master;
            return first_slave;
        }
        case MWC_DOWN: {
            if(master || last_slave == NULL) return first_master;
            return last_slave;
        }
        case MWC_LEFT: {
            return first_master;
        }
        case MWC_RIGHT: {
            if(last_slave != NULL) return last_slave;
            return last_master;
        }
    }
}

static struct mwc_toplevel *
layout_toplevel_at(struct mwc_workspace *workspace, int32_t x, int32_t y) {
    struct mwc_toplevel *t;
    wl_list_for_each(t, &workspace->masters, link) {
        struct wlr_box box = t->deco_box;
        int32_t rx = 0, ry = 0;

        if(&t->link == workspace->masters.next) {
            rx -= server.config->outer_gaps;
            ry -= server.config->outer_gaps;
            if(&t->link == workspace->masters.prev) {
                box.width += 2 * server.config->outer_gaps;
            } else {
                box.width += server.config->outer_gaps + server.config->inner_gaps;
            }
            box.height += 2 * server.config->outer_gaps;
        } else if(&t->link == workspace->masters.prev && wl_list_empty(&workspace->slaves)) {
            rx -= server.config->inner_gaps;
            ry -= server.config->outer_gaps;
            box.width += server.config->inner_gaps + server.config->outer_gaps;
            box.height += 2 * server.config->outer_gaps;
        } else {
            rx -= server.config->inner_gaps;
            ry -= server.config->outer_gaps;
            box.width += 2 * server.config->inner_gaps;
            box.height += 2 * server.config->outer_gaps;
        }

        box.x += rx;
        box.y += ry;

        if(wlr_box_contains_point(&box, x, y)) {
            return t;
        }
    };

    wl_list_for_each(t, &workspace->slaves, link) {
        struct wlr_box box = t->deco_box;
        int32_t rx = 0, ry = 0;

        if(&t->link == workspace->slaves.next) {
            rx -= server.config->inner_gaps;
            ry -= server.config->outer_gaps;
            box.width += server.config->inner_gaps + server.config->outer_gaps;
            if(&t->link == workspace->slaves.prev) {
                box.height += 2 * server.config->outer_gaps;
            } else {
                box.height += server.config->inner_gaps + server.config->outer_gaps;
            }
        } else if(&t->link == workspace->slaves.prev) {
            rx -= server.config->inner_gaps;
            ry -= server.config->inner_gaps;
            box.width += server.config->inner_gaps + server.config->outer_gaps;
            box.height += server.config->inner_gaps + server.config->outer_gaps;
        } else {
            rx -= server.config->inner_gaps;
            ry -= server.config->inner_gaps;
            box.width += server.config->inner_gaps + server.config->outer_gaps;
            box.height += 2 * server.config->inner_gaps;
        }

        box.x += rx;
        box.y += ry;

        if(wlr_box_contains_point(&box, x, y)) {
            return t;
        }
    }

    return NULL;
}

void
layout_insert_toplevel_at(struct mwc_toplevel *toplevel, uint32_t x, uint32_t y) {
    struct mwc_workspace *workspace = server.active_workspace;

    toplevel->workspace = workspace;

    struct mwc_toplevel *under_cursor = layout_toplevel_at(workspace, x, y);

    if(under_cursor == NULL) {
        if(wl_list_length(&workspace->masters) < server.config->master_count) {
            wl_list_insert(workspace->masters.prev, &toplevel->link);
        } else {
            wl_list_insert(workspace->slaves.prev, &toplevel->link);
        }
    } else {
        bool on_left_side = x <= under_cursor->deco_box.x + under_cursor->deco_box.width / 2;
        bool on_top_side = y <= under_cursor->deco_box.y + under_cursor->deco_box.height / 2;
        bool under_cursor_is_master = toplevel_is_master(under_cursor);

        // we insert it before under_cursor if either:
        // - its last master and there are some slaves
        // - cursor is on left (top) */
        if((under_cursor_is_master && &under_cursor->link == workspace->masters.prev &&
                   wl_list_length(&workspace->slaves) > 0) ||
                (under_cursor_is_master && on_left_side) || (!under_cursor_is_master && on_top_side)) {
            wl_list_insert(under_cursor->link.prev, &toplevel->link);
        } else {
            wl_list_insert(&under_cursor->link, &toplevel->link);
        }

        if(wl_list_length(&workspace->masters) > server.config->master_count) {
            struct mwc_toplevel *last = wl_container_of(workspace->masters.prev, last, link);
            wl_list_remove(&last->link);
            wl_list_insert(workspace->slaves.prev, &last->link);
        }
    }

    // finally, we set this as a new state
    layout_configure(workspace);
}
