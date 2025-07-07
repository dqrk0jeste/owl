#include "constraint.h"

#include <assert.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/region.h>

#include "mwc.h"
#include "toplevel.h"

extern struct server server;

static void
move_to_hint(struct wlr_pointer_constraint_v1 *constraint) {
    assert(constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED);

    if(constraint->current.committed & WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT) {
        double sx = constraint->current.cursor_hint.x;
        double sy = constraint->current.cursor_hint.y;
        wlr_cursor_warp(server.cursor.base, NULL, server.focused_toplevel->scene_tree->node.x + sx,
                server.focused_toplevel->scene_tree->node.y + sy);

        // make sure we are not sending unnecessary surface movements (took from labwc)
        wlr_seat_pointer_warp(server.seat.base, sx, sy);
    }
}

void
constraint_remove_current() {
    wlr_pointer_constraint_v1_send_deactivated(server.constraint_manager.current_constraint);
    wl_list_remove(&server.constraint_manager.current_constraint_destroy.link);

    server.constraint_manager.current_constraint = NULL;
}

void
constraint_set_as_current(struct wlr_pointer_constraint_v1 *constraint) {
    if(server.constraint_manager.current_constraint == constraint)
        return;

    if(server.constraint_manager.current_constraint != NULL) {
        constraint_remove_current();
    }

    server.constraint_manager.current_constraint = constraint;
    wl_signal_add(&constraint->events.destroy, &server.constraint_manager.current_constraint_destroy);

    if(constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        move_to_hint(constraint);
    }

    wlr_pointer_constraint_v1_send_activated(constraint);
}

void
constraint_apply_to_move(double *dx, double *dy) {
    if(server.constraint_manager.current_constraint == NULL)
        return;

    if(server.constraint_manager.current_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        *dx = *dy = 0;
    } else {
        if(server.seat.base->pointer_state.focused_surface == NULL)
            return;

        double current_x = server.seat.base->pointer_state.sx;
        double current_y = server.seat.base->pointer_state.sy;

        double constrained_x, constrained_y;
        if(wlr_region_confine(&server.constraint_manager.current_constraint->region, current_x, current_y,
                   current_x + *dx, current_y + *dy, &constrained_x, &constrained_y)) {
            *dx = constrained_x - current_x;
            *dy = constrained_y - current_y;
        }
    }
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    // todo: test if this is needed
    if(server.constraint_manager.current_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        move_to_hint(server.constraint_manager.current_constraint);
    }

    server.constraint_manager.current_constraint = NULL;
}

void
constraint_manager_init(void) {
    server.constraint_manager.base = wlr_pointer_constraints_v1_create(server.display);

    // this will be wired when we apply the constraint
    server.constraint_manager.current_constraint_destroy.notify = handle_destroy;
}
