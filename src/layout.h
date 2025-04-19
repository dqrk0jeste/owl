#pragma once

#include <stdint.h>

#include "mwc.h"
#include "output.h"

void
layout_get_masters_container_size(struct mwc_workspace *workspace, uint32_t master_count, uint32_t slave_count,
        uint32_t *width, uint32_t *height);

void
layout_get_slaves_container_size(struct mwc_workspace *workspace, uint32_t slave_count, uint32_t *width,
        uint32_t *height);

bool
toplevel_is_master(struct mwc_toplevel *toplevel);

bool
toplevel_is_slave(struct mwc_toplevel *toplevel);

void
layout_configure(struct mwc_workspace *workspace);

// this function assumes they are in the same workspace and that t2 comes after t1 if in the same list
void
layout_swap_toplevels(struct mwc_toplevel *t1, struct mwc_toplevel *t2);

struct mwc_toplevel *
layout_find_closest_toplevel(struct mwc_workspace *workspace, bool master, enum mwc_direction side);

void
layout_insert_toplevel_at(struct mwc_toplevel *toplevel, uint32_t x, uint32_t y);
