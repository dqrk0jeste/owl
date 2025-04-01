#pragma once

#include <stdint.h>
#include <wlr/util/box.h>
#include <wlr/types/wlr_scene.h>

struct mwc_toplevel;

void
toplevel_draw_decorations(struct mwc_toplevel *toplevel);

struct mwc_workspace;

void
workspace_draw_frame(struct mwc_workspace *workspace);

