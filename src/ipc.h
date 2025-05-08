#pragma once

#include <stdbool.h>

#include "ipc_shared.h"

enum ipc_event {
    IPC_ACTIVE_WORKSPACE,
    IPC_ACTIVE_TOPLEVEL,
    IPC_EVENT_COUNT,
};

void
ipc_broadcast_message(enum ipc_event event);

void
ipc_init(void);

void
ipc_deinit(void);

bool
ipc_running(void);
