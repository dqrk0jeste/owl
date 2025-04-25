#include "layout.h"

#include <stdint.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>

#include "config.h"
#include "mwc.h"
#include "toplevel.h"
#include "wlr/util/box.h"

extern struct server server;

void
layout_get_masters_container_size(struct workspace *workspace, uint32_t master_count, uint32_t slave_count,
        uint32_t *width, uint32_t *height) {
    uint32_t outer_gaps = server.config->outer_gaps;
    uint32_t inner_gaps = server.config->inner_gaps;
    double master_ratio = workspace->master_ratio;

    struct wlr_box output_box = workspace->output->usable_area;

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
layout_get_slaves_container_size(struct workspace *workspace, uint32_t slave_count, uint32_t *width, uint32_t *height) {
    uint32_t outer_gaps = server.config->outer_gaps;
    uint32_t inner_gaps = server.config->inner_gaps;
    double master_ratio = workspace->master_ratio;

    struct wlr_box output_box = workspace->output->usable_area;

    uint32_t total_gaps = outer_gaps  // top outer gaps
            + (slave_count - 1) * 2 * inner_gaps  // inner gaps between slaves
            + outer_gaps;  // bottom outer gaps

    *width = output_box.width * (1 - master_ratio) - outer_gaps - inner_gaps;
    *height = (output_box.height - total_gaps) / slave_count;
}

bool
has_masters(struct workspace *workspace) {
    return !wl_list_empty(&workspace->masters);
}

bool
has_slaves(struct workspace *workspace) {
    return !wl_list_empty(&workspace->slaves);
}

struct toplevel *
next_master(struct toplevel *toplevel) {
    if(toplevel->link.next == toplevel->workspace->masters.prev)
        return NULL;

    struct toplevel *t = wl_container_of(toplevel->link.next, t, link);
    return t;
}

struct toplevel *
prev_master(struct toplevel *toplevel) {
    if(toplevel->link.prev == toplevel->workspace->masters.next)
        return NULL;

    struct toplevel *t = wl_container_of(toplevel->link.prev, t, link);
    return t;
}

struct toplevel *
next_slave(struct toplevel *toplevel) {
    if(toplevel->link.next == toplevel->workspace->slaves.prev)
        return NULL;

    struct toplevel *t = wl_container_of(toplevel->link.next, t, link);
    return t;
}

struct toplevel *
prev_slave(struct toplevel *toplevel) {
    if(toplevel->link.prev == toplevel->workspace->slaves.next)
        return NULL;

    struct toplevel *t = wl_container_of(toplevel->link.prev, t, link);
    return t;
}

struct toplevel *
first_master(struct workspace *workspace) {
    if(wl_list_empty(&workspace->masters))
        return NULL;

    struct toplevel *t = wl_container_of(workspace->masters.next, t, link);
    return t;
}

struct toplevel *
last_master(struct workspace *workspace) {
    if(wl_list_empty(&workspace->masters))
        return NULL;

    struct toplevel *t = wl_container_of(workspace->masters.prev, t, link);
    return t;
}

struct toplevel *
first_slave(struct workspace *workspace) {
    if(wl_list_empty(&workspace->slaves))
        return NULL;

    struct toplevel *t = wl_container_of(workspace->slaves.next, t, link);
    return t;
}

struct toplevel *
last_slave(struct workspace *workspace) {
    if(wl_list_empty(&workspace->slaves))
        return NULL;

    struct toplevel *t = wl_container_of(workspace->slaves.prev, t, link);
    return t;
}

void
demote_last_master(struct workspace *workspace) {
    struct toplevel *last = last_master(workspace);
    wl_list_remove(&last->link);
    wl_list_insert(workspace->slaves.prev, &last->link);
    last->mode = TOPLEVEL_MODE_SLAVE;
}

void
promote_last_slave(struct workspace *workspace) {
    struct toplevel *last = last_slave(workspace);
    wl_list_remove(&last->link);
    wl_list_insert(workspace->masters.prev, &last->link);
    last->mode = TOPLEVEL_MODE_MASTER;
}

void
layout_add(struct workspace *workspace, struct toplevel *toplevel) {
    toplevel->workspace = workspace;
    if(wl_list_length(&workspace->masters) < server.config->master_count) {
        wl_list_insert(&workspace->masters, &toplevel->link);
        toplevel->mode = TOPLEVEL_MODE_MASTER;
    } else {
        wl_list_insert(&workspace->slaves, &toplevel->link);
        toplevel->mode = TOPLEVEL_MODE_SLAVE;
    }
}

void
layout_configure(struct workspace *workspace) {
    // if there are no masters we are done
    if(wl_list_empty(&workspace->masters))
        return;

    struct output *output = workspace->output;

    uint32_t outer_gaps = server.config->outer_gaps;
    uint32_t inner_gaps = server.config->inner_gaps;

    uint32_t slave_count = wl_list_length(&workspace->slaves);
    uint32_t master_count = wl_list_length(&workspace->masters);

    uint32_t width, height;
    layout_get_masters_container_size(workspace, master_count, slave_count, &width, &height);

    struct wlr_box box = {.width = width, .height = height};

    size_t i = 0;
    struct toplevel *toplevel;
    wl_list_for_each(toplevel, &workspace->masters, link) {
        box.x = output->usable_area.x + outer_gaps + box.width * i + inner_gaps * 2 * i;
        box.y = output->usable_area.y + outer_gaps;

        toplevel_set_state(toplevel, box);
        i++;
    }

    if(slave_count == 0)
        return;

    layout_get_slaves_container_size(workspace, slave_count, &width, &height);

    box.width = width;
    box.height = height;

    i = 0;
    wl_list_for_each(toplevel, &workspace->slaves, link) {
        box.x = output->usable_area.x + output->usable_area.width * workspace->master_ratio + inner_gaps;
        box.y = output->usable_area.y + outer_gaps + height * i + inner_gaps * 2 * i;

        toplevel_set_state(toplevel, box);
        i++;
    }
}

void
layout_swap_toplevels(struct toplevel *t1, struct toplevel *t2) {
    // swap them
    struct wl_list *before_t1 = t1->link.prev;
    wl_list_remove(&t1->link);
    wl_list_insert(&t2->link, &t1->link);
    wl_list_remove(&t2->link);
    wl_list_insert(before_t1, &t2->link);

    // and switch their modes around
    enum toplevel_mode temp = t1->mode;
    t1->mode = t2->mode;
    t2->mode = temp;

    layout_configure(t1->workspace);
}

// struct toplevel *
// layout_find_closest_toplevel(struct workspace *workspace, bool master, enum direction side) {
//     // this means there are no tiled toplevels
//     if(wl_list_empty(&workspace->masters))
//         return NULL;
//
//     // struct toplevel *first_master = wl_container_of(workspace->masters.next, first_master, link);
//     // struct toplevel *last_master = wl_container_of(workspace->masters.prev, last_master, link);
//     //
//     // struct toplevel *first_slave = NULL;
//     // struct toplevel *last_slave = NULL;
//     // if(!wl_list_empty(&workspace->slaves)) {
//     //     first_slave = wl_container_of(workspace->slaves.next, first_slave, link);
//     //     last_slave = wl_container_of(workspace->slaves.prev, last_slave, link);
//     // }
//
//     switch(side) {
//         case DIRECTION_UP: {
//             if(master || first_slave == NULL)
//                 return first_master;
//             return first_slave;
//         }
//         case DIRECTION_DOWN: {
//             if(master || last_slave == NULL)
//                 return first_master;
//             return last_slave;
//         }
//         case DIRECTION_LEFT: {
//             return first_master;
//         }
//         case DIRECTION_RIGHT: {
//             if(last_slave != NULL)
//                 return last_slave;
//             return last_master;
//         }
//     }
// }

static struct toplevel *
layout_toplevel_at(struct workspace *workspace, int32_t x, int32_t y) {
    struct toplevel *iter;
    wl_list_for_each(iter, &workspace->masters, link) {
        struct wlr_box box = iter->deco_box;
        int32_t rx = 0, ry = 0;

        if(iter == first_master(workspace)) {
            rx -= server.config->outer_gaps;
            ry -= server.config->outer_gaps;
            if(iter == last_master(workspace)) {
                box.width += 2 * server.config->outer_gaps;
            } else {
                box.width += server.config->outer_gaps + server.config->inner_gaps;
            }
            box.height += 2 * server.config->outer_gaps;
        } else if(iter == last_master(workspace) && !has_slaves(workspace)) {
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
            return iter;
        }
    };

    wl_list_for_each(iter, &workspace->slaves, link) {
        struct wlr_box box = iter->deco_box;
        int32_t rx = 0, ry = 0;

        if(iter == first_slave(workspace)) {
            rx -= server.config->inner_gaps;
            ry -= server.config->outer_gaps;
            box.width += server.config->inner_gaps + server.config->outer_gaps;
            if(iter == last_slave(workspace)) {
                box.height += 2 * server.config->outer_gaps;
            } else {
                box.height += server.config->inner_gaps + server.config->outer_gaps;
            }
        } else if(iter == last_slave(workspace)) {
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
            return iter;
        }
    }

    return NULL;
}

void
layout_insert_toplevel_at(struct toplevel *toplevel, uint32_t x, uint32_t y) {
    struct workspace *workspace = server.active_workspace;

    toplevel->workspace = workspace;

    struct toplevel *under_cursor = layout_toplevel_at(workspace, x, y);

    if(under_cursor == NULL) {
        layout_add(workspace, toplevel);
    } else {
        bool on_left_side = x <= under_cursor->deco_box.x + under_cursor->deco_box.width / 2;
        bool on_top_side = y <= under_cursor->deco_box.y + under_cursor->deco_box.height / 2;
        bool under_cursor_is_master = under_cursor->mode == TOPLEVEL_MODE_MASTER;

        // we insert it before under_cursor if either:
        // - its last master and there are some slaves
        // - cursor is on left (top)
        if((under_cursor_is_master && under_cursor == last_master(workspace) && has_slaves(workspace)) ||
                (under_cursor_is_master && on_left_side) || (!under_cursor_is_master && on_top_side)) {
            wl_list_insert(under_cursor->link.prev, &toplevel->link);
        } else {
            wl_list_insert(&under_cursor->link, &toplevel->link);
        }

        toplevel->mode = under_cursor->mode;

        // if there are more masters than needed, we demote one of them
        if(wl_list_length(&workspace->masters) > server.config->master_count) {
            demote_last_master(workspace);
        }
    }

    // finally, we set this as a new state
    layout_configure(workspace);
}
