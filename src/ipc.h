#include "ipc_shared.h"

enum ipc_event {
    IPC_ACTIVE_WORKSPACE,
    IPC_ACTIVE_TOPLEVEL,
    IPC_EVENT_COUNT,
};

void
ipc_broadcast_message(enum ipc_event event);

void *
ipc_run(void *args);
