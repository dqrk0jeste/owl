#pragma once

#include "output.h"
#include "owl.h"

#include <stdint.h>

void
calculate_masters_dimensions(struct owl_output *output, uint32_t master_count,
                             uint32_t slave_count, uint32_t *width, uint32_t *height);

void
calculate_slaves_dimensions(struct owl_output *output, uint32_t slave_count,
                            uint32_t *width, uint32_t *height);

bool
toplevel_is_master(struct owl_toplevel *toplevel);

bool
layout_tiled_ready(struct owl_workspace *workspace);

void
layout_commit(struct owl_workspace *workspace);

void
layout_send_configure(struct owl_workspace *workspace);

/* this function assumes they are in the same workspace and
 * that t2 comes after t1 if in the same list */
void
layout_swap_tiled_toplevels(struct owl_toplevel *t1,
                            struct owl_toplevel *t2);

struct owl_toplevel *
layout_find_closest_tiled_toplevel(struct owl_workspace *workspace, bool master,
                                   enum owl_direction side);

struct owl_toplevel *
layout_find_closest_floating_toplevel(struct owl_workspace *workspace,
                                      enum owl_direction side);
