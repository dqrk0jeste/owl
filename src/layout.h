#pragma once

#include <stdint.h>

#include "toplevel.h"
#include "workspace.h"

void
layout_get_masters_container_size(struct workspace *workspace, uint32_t master_count, uint32_t slave_count,
        uint32_t *width, uint32_t *height);

void
layout_get_slaves_container_size(struct workspace *workspace, uint32_t slave_count, uint32_t *width, uint32_t *height);

struct toplevel *
next_master(struct toplevel *toplevel);

struct toplevel *
prev_master(struct toplevel *toplevel);

struct toplevel *
next_slave(struct toplevel *toplevel);

struct toplevel *
prev_slave(struct toplevel *toplevel);

struct toplevel *
first_master(struct workspace *workspace);

struct toplevel *
last_master(struct workspace *workspace);

struct toplevel *
first_slave(struct workspace *workspace);

struct toplevel *
last_slave(struct workspace *workspace);

void
demote_last_master(struct workspace *workspace);

void
promote_last_slave(struct workspace *workspace);

bool
has_masters(struct workspace *workspace);

bool
has_slaves(struct workspace *workspace);

void
layout_add(struct workspace *workspace, struct toplevel *toplevel);

void
layout_configure(struct workspace *workspace);

// this function assumes they are in the same workspace and that t2 comes after t1 if in the same list
void
layout_swap(struct toplevel *t1, struct toplevel *t2);

// struct toplevel *
// layout_find_closest_toplevel(struct workspace *workspace, bool master, enum direction side);

void
layout_insert_toplevel_at(struct toplevel *toplevel, uint32_t x, uint32_t y);
