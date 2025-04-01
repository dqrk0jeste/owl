#pragma once

#include "output.h"
#include "mwc.h"

#include <stdint.h>

void
layout_get_masters_container_size(struct mwc_output *output, uint32_t master_count,
        uint32_t slave_count, uint32_t *width, uint32_t *height);

void
layout_get_slaves_container_size(struct mwc_output *output, uint32_t slave_count,
        uint32_t *width, uint32_t *height);

bool
toplevel_is_master(struct mwc_toplevel *toplevel);

bool
toplevel_is_slave(struct mwc_toplevel *toplevel);

void
layout_set_pending_state(struct mwc_workspace *workspace);

// this function assumes they are in the same workspace and that t2 comes after t1 if in the same list
void
layout_swap_toplevels(struct mwc_toplevel *t1, struct mwc_toplevel *t2);

struct mwc_toplevel *
layout_find_closest_toplevel(struct mwc_workspace *workspace, bool master,
        enum mwc_direction side);

struct mwc_toplevel *
layout_toplevel_at(struct mwc_workspace *workspace, int32_t x, int32_t y);

