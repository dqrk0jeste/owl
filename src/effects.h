#pragma once

#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <wlr/util/box.h>

struct output;

void
output_draw(struct output *output);
