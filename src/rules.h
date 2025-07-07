#pragma once

#include <regex.h>
#include <stdbool.h>
#include <wayland-util.h>

#include "config.h"
#include "layer_shell.h"
#include "toplevel.h"

bool
toplevel_matches_rule(struct toplevel *toplevel, struct toplevel_config *config);

bool
layer_surface_matches_rule(struct layer_surface *layer_surface, struct layer_config *config);

// update the toplevel rules for this toplevel, applying the decorations and setting the appropriate params. if
// `configure` is true, will new send size configure to the toplevel
void
rules_update_for_toplevel(struct toplevel *toplevel, bool configure);

// same, but for layer surfaces
void
rules_update_for_layer_surface(struct layer_surface *layer_surface);

enum toplevel_default_mode
rules_get_toplevel_default_mode(struct toplevel *toplevel);
