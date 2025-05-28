#include "layout.h"

#include <assert.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include "array.h"
#include "config.h"
#include "mwc.h"
#include "rules.h"
#include "toplevel.h"

extern struct server server;

static void
get_gaps(int master_count, int slave_count, char *output, struct gaps *inner, struct gaps *outer) {
    uint32_t found = 0;
    for(struct gaps_config *iter = array_last(server.config->gaps); iter >= server.config->gaps; iter--) {
        if(((iter->specified & GAPS_FIELD_MATCH_OUTPUT) && strcmp(iter->output, output) != 0) ||
                ((iter->specified & GAPS_FIELD_MATCH_MASTER_COUNT) &&
                        !matches_relation(iter->master_relation, master_count, iter->master_count)) ||
                ((iter->specified & GAPS_FIELD_MATCH_SLAVE_COUNT) &&
                        !matches_relation(iter->slave_relation, slave_count, iter->slave_count)))
            continue;

        if(!(found & GAPS_FIELD_INNER) && (iter->specified & GAPS_FIELD_INNER)) {
            *inner = iter->inner;
            found |= GAPS_FIELD_INNER;
        }
        if(!(found & GAPS_FIELD_OUTER) && (iter->specified & GAPS_FIELD_OUTER)) {
            *outer = iter->outer;
            found |= GAPS_FIELD_OUTER;
        }
    }

    assert(found & GAPS_FIELD_INNER);
    assert(found & GAPS_FIELD_OUTER);
}

void
layout_get_masters_container_size(struct workspace *workspace, int master_count, int slave_count, int *width,
        int *height) {
    struct gaps inner_gaps, outer_gaps;
    get_gaps(master_count, slave_count, workspace->output->wlr_output->name, &inner_gaps, &outer_gaps);

    double master_ratio = workspace->master_ratio;

    struct wlr_box output_box = workspace->output->usable_area;

    int total_width = slave_count > 0 ? output_box.width * master_ratio - outer_gaps.left
                                      : output_box.width - outer_gaps.left - outer_gaps.right;

    *width = total_width / master_count - inner_gaps.right - inner_gaps.left;
    *height = output_box.height - outer_gaps.top - outer_gaps.bottom - inner_gaps.top - outer_gaps.bottom;
}

void
layout_get_slaves_container_size(struct workspace *workspace, int master_count, int slave_count, int *width,
        int *height) {
    struct gaps inner_gaps, outer_gaps;
    get_gaps(master_count, slave_count, workspace->output->wlr_output->name, &inner_gaps, &outer_gaps);
    double master_ratio = workspace->master_ratio;

    struct wlr_box output_box = workspace->output->usable_area;

    *width = output_box.width * (1 - master_ratio) - outer_gaps.left - inner_gaps.right - inner_gaps.left;
    *height =
            (output_box.height - outer_gaps.top - outer_gaps.bottom) / slave_count - inner_gaps.top - inner_gaps.bottom;
}

bool
has_masters(struct workspace *workspace) {
    return workspace->master_count > 0;
}

bool
has_slaves(struct workspace *workspace) {
    return workspace->slave_count > 0;
}

struct toplevel *
next_master(struct toplevel *toplevel) {
    if(toplevel->link.next == &toplevel->workspace->masters)
        return NULL;

    struct toplevel *t = wl_container_of(toplevel->link.next, t, link);
    return t;
}

struct toplevel *
prev_master(struct toplevel *toplevel) {
    if(toplevel->link.prev == &toplevel->workspace->masters)
        return NULL;

    struct toplevel *t = wl_container_of(toplevel->link.prev, t, link);
    return t;
}

struct toplevel *
next_slave(struct toplevel *toplevel) {
    if(toplevel->link.next == &toplevel->workspace->slaves)
        return NULL;

    struct toplevel *t = wl_container_of(toplevel->link.next, t, link);
    return t;
}

struct toplevel *
prev_slave(struct toplevel *toplevel) {
    if(toplevel->link.prev == &toplevel->workspace->slaves)
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
    workspace->master_count--;
    workspace->slave_count++;
}

void
promote_last_slave(struct workspace *workspace) {
    struct toplevel *last = last_slave(workspace);
    wl_list_remove(&last->link);
    wl_list_insert(workspace->masters.prev, &last->link);
    last->mode = TOPLEVEL_MODE_MASTER;
    workspace->master_count++;
    workspace->slave_count--;
}

void
layout_add(struct workspace *workspace, struct toplevel *toplevel) {
    toplevel->workspace = workspace;
    if(workspace->master_count < workspace->output->master_count) {
        wl_list_insert(workspace->masters.prev, &toplevel->link);
        toplevel->mode = TOPLEVEL_MODE_MASTER;
        workspace->master_count++;
    } else {
        wl_list_insert(workspace->slaves.prev, &toplevel->link);
        toplevel->mode = TOPLEVEL_MODE_SLAVE;
        workspace->slave_count++;
    }
}

void
layout_configure(struct workspace *workspace) {
    // if there are no masters we are done
    if(wl_list_empty(&workspace->masters))
        return;

    struct output *output = workspace->output;

    struct gaps inner_gaps, outer_gaps;
    get_gaps(workspace->master_count, workspace->slave_count, workspace->output->wlr_output->name, &inner_gaps,
            &outer_gaps);
    workspace->inner_gaps = inner_gaps;
    workspace->outer_gaps = outer_gaps;

    // remove the outer gaps from the area
    struct wlr_box layout_area = output->usable_area;
    layout_area.x += outer_gaps.left;
    layout_area.y += outer_gaps.top;
    layout_area.width -= outer_gaps.left + outer_gaps.right;
    layout_area.height -= outer_gaps.top + outer_gaps.bottom;

    // calculate master width and height (including inner gaps)
    int width = workspace->slave_count > 0 ? layout_area.width * workspace->master_ratio : layout_area.width;
    width /= workspace->master_count;
    int height = layout_area.height;

    int i = 0;
    struct toplevel *toplevel;
    wl_list_for_each(toplevel, &workspace->masters, link) {
        struct wlr_box box = {
                layout_area.x + i * width + inner_gaps.left,
                layout_area.y + inner_gaps.top,
                width - inner_gaps.left - inner_gaps.right,
                height - inner_gaps.top - inner_gaps.bottom,
        };

        rules_update_for_toplevel(toplevel);
        toplevel_set_state(toplevel, box);
        i++;
    }

    if(workspace->slave_count == 0)
        return;

    width = layout_area.width * (1 - workspace->master_ratio);
    height = layout_area.height / workspace->slave_count;

    i = 0;
    wl_list_for_each(toplevel, &workspace->slaves, link) {
        struct wlr_box box = {
                layout_area.x + layout_area.width * workspace->master_ratio + inner_gaps.left,
                layout_area.y + i * height + inner_gaps.top,
                width - inner_gaps.left - inner_gaps.right,
                height - inner_gaps.top - inner_gaps.bottom,
        };

        rules_update_for_toplevel(toplevel);
        toplevel_set_state(toplevel, box);
        i++;
    }
}

void
layout_swap(struct toplevel *t1, struct toplevel *t2) {
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

static struct toplevel *
layout_toplevel_at(struct workspace *workspace, int x, int y) {
    struct toplevel *iter;
    wl_list_for_each(iter, &workspace->masters, link) {
        struct wlr_box box = iter->deco_box;
        box.x -= workspace->inner_gaps.left;
        box.y -= workspace->inner_gaps.top;
        box.width += workspace->inner_gaps.left + workspace->inner_gaps.right;
        box.height += workspace->inner_gaps.top + workspace->inner_gaps.bottom;

        if(wlr_box_contains_point(&box, x, y)) {
            return iter;
        }
    };

    wl_list_for_each(iter, &workspace->slaves, link) {
        struct wlr_box box = iter->deco_box;
        box.x -= workspace->inner_gaps.left;
        box.y -= workspace->inner_gaps.top;
        box.width += workspace->inner_gaps.left + workspace->inner_gaps.right;
        box.height += workspace->inner_gaps.top + workspace->inner_gaps.bottom;

        if(wlr_box_contains_point(&box, x, y)) {
            return iter;
        }
    }

    return NULL;
}

void
layout_insert_toplevel_at(struct toplevel *toplevel, int x, int y) {
    struct workspace *workspace = server.active_workspace;

    toplevel->workspace = workspace;

    struct toplevel *under_cursor = layout_toplevel_at(workspace, x, y);

    if(under_cursor == NULL) {
        layout_add(workspace, toplevel);
    } else if(under_cursor->mode == TOPLEVEL_MODE_MASTER) {
        if((under_cursor == last_master(workspace) && has_slaves(workspace)) ||
                x <= under_cursor->deco_box.x + under_cursor->deco_box.width / 2) {
            wl_list_insert(under_cursor->link.prev, &toplevel->link);
        } else {
            // else insert after
            wl_list_insert(&under_cursor->link, &toplevel->link);
        }

        toplevel->mode = TOPLEVEL_MODE_MASTER;
        workspace->master_count++;
        if(workspace->master_count > workspace->output->master_count) {
            // if there are more masters than needed, we demote one of them
            demote_last_master(workspace);
        }
    } else {
        if(y <= under_cursor->deco_box.y + under_cursor->deco_box.height / 2) {
            // if on top insert before
            wl_list_insert(under_cursor->link.prev, &toplevel->link);
        } else {
            // else insert after
            wl_list_insert(&under_cursor->link, &toplevel->link);
        }

        toplevel->mode = TOPLEVEL_MODE_SLAVE;
        workspace->slave_count++;
    }

    // finally, we set this as a new state
    layout_configure(workspace);
}
