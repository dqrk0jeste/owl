#pragma once

#include <stdbool.h>

#include "ipc_shared.h"

void
ipc_init(void);

void
ipc_deinit(void);

bool
ipc_running(void);

void
ipc_send_active_workspace(void);

void
ipc_send_focused_toplevel(void);

void
ipc_send_focused_layer(void);
