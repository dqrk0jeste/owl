#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>

struct constraint_manager {
    struct wlr_pointer_constraints_v1 *base;

    struct wl_listener new_contraint;

    struct wlr_pointer_constraint_v1 *current_constraint;
    struct wl_listener current_constraint_destroy;
};

void
constraint_manager_init(void);

void
constraint_set_as_current(struct wlr_pointer_constraint_v1 *constraint);

void
constraint_apply_to_move(double *dx, double *dy);
