#pragma once

#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <wlr/util/box.h>

struct mwc_output;

void
output_draw(struct mwc_output *output);
