#pragma once

#include <stdint.h>
#include <wlr/util/box.h>
#include <wlr/types/wlr_scene.h>

struct mwc_output;

void
output_draw(struct mwc_output *output);

