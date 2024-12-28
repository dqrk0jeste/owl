#include "workspace.h"

#include "layout.h"
#include "owl.h"
#include "ipc.h"
#include "keybinds.h"

#include <assert.h>
#include <stdlib.h>

extern struct owl_server server;

void
workspace_create_for_output(struct owl_output *output, struct workspace_config *config) {
  struct owl_workspace *workspace = calloc(1, sizeof(*workspace));

  wl_list_init(&workspace->floating_toplevels);
  wl_list_init(&workspace->masters);
  wl_list_init(&workspace->slaves);
  workspace->output = output;
  workspace->index = config->index;

  wl_list_insert(&output->workspaces, &workspace->link);

  /* if first then set it active */
  if(output->active_workspace == NULL) {
    output->active_workspace = workspace;
  }

  struct keybind *k;
  wl_list_for_each(k, &server.config->keybinds, link) {
    /* we didnt have information about what workspace this is going to be,
     * so we only kept an index. now we replace it with
     * the actual workspace pointer */
    if(k->action == keybind_change_workspace
       && (uint64_t)k->args == workspace->index) {
      k->args = workspace;
      k->initialized = true;
    } else if(k->action == keybind_move_focused_toplevel_to_workspace
              && (uint64_t)k->args == workspace->index) {
      k->args = workspace;
      k->initialized = true;
    }
  }
}

void
change_workspace(struct owl_workspace *workspace, bool keep_focus) {
  /* if it is the same as global active workspace, do nothing */
  if(server.active_workspace == workspace) return;

  /* if it is an already active on its output, just switch to it */
  if(workspace == workspace->output->active_workspace) {
    server.active_workspace = workspace;
    cursor_jump_output(workspace->output);
    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);
    /* we dont want to keep focus only if it is going to be under a fullscreen toplevel */
    if(workspace->fullscreen_toplevel != NULL) {
      focus_toplevel(workspace->fullscreen_toplevel);
    } else if(keep_focus) {
      return;
    } else if(!wl_list_empty(&workspace->masters)) {
      struct owl_toplevel *t = wl_container_of(workspace->masters.next, t, link);
      focus_toplevel(t);
    } else if(!wl_list_empty(&workspace->floating_toplevels)) {
      struct owl_toplevel *t = wl_container_of(workspace->floating_toplevels.next, t, link);
      focus_toplevel(t);
    } else {
      unfocus_focused_toplevel();
    }
    return;
  }

  /* else remove all the toplevels on that workspace */
  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->output->active_workspace->floating_toplevels, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
  }
  wl_list_for_each(t, &workspace->output->active_workspace->masters, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
  }
  wl_list_for_each(t, &workspace->output->active_workspace->slaves, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
  }

  /* and show this workspace's toplevels */
  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
  }
  wl_list_for_each(t, &workspace->masters, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
  }
  wl_list_for_each(t, &workspace->slaves, link) {
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
  }

  if(server.active_workspace->output != workspace->output) {
    cursor_jump_output(workspace->output);
  }

  server.active_workspace = workspace;
  workspace->output->active_workspace = workspace;

  ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);

  /* same as above */
  if(workspace->fullscreen_toplevel != NULL) {
    focus_toplevel(workspace->fullscreen_toplevel);
  } else if(keep_focus) {
    return;
  } else if(!wl_list_empty(&workspace->masters)) {
    struct owl_toplevel *t = wl_container_of(workspace->masters.next, t, link);
    focus_toplevel(t);
  } else if(!wl_list_empty(&workspace->floating_toplevels)) {
    struct owl_toplevel *t = wl_container_of(workspace->floating_toplevels.next, t, link);
    focus_toplevel(t);
  } else {
    unfocus_focused_toplevel();
  }
}

void
toplevel_move_to_workspace(struct owl_toplevel *toplevel,
                           struct owl_workspace *workspace) {
  assert(toplevel != NULL && workspace != NULL);
  if(toplevel->workspace == workspace) return;

  struct owl_workspace *old_workspace = toplevel->workspace;

  /* handle server state; note: even tho fullscreen toplevel is handled differently
   * we will still update its underlying type */
  if(toplevel->floating) {
    toplevel->workspace = workspace;
    wl_list_remove(&toplevel->link);
    wl_list_insert(&workspace->floating_toplevels, &toplevel->link);
  } else if(toplevel_is_master(toplevel)){
    wl_list_remove(&toplevel->link);
    if(!wl_list_empty(&old_workspace->slaves)) {
      struct owl_toplevel *s = wl_container_of(old_workspace->slaves.next, s, link);
      wl_list_remove(&s->link);
      wl_list_insert(old_workspace->masters.prev, &s->link);
    }

    toplevel->workspace = workspace;
    if(wl_list_length(&workspace->masters) < server.config->master_count) {
      wl_list_insert(workspace->masters.prev, &toplevel->link);
    } else {
      wl_list_insert(workspace->slaves.prev, &toplevel->link);
    }
  } else {
    wl_list_remove(&toplevel->link);

    toplevel->workspace = workspace;
    if(wl_list_length(&workspace->masters) < server.config->master_count) {
      wl_list_insert(workspace->masters.prev, &toplevel->link);
    } else {
      wl_list_insert(workspace->slaves.prev, &toplevel->link);
    }
  }

  /* handle rendering */
  if(toplevel->fullscreen) {
    old_workspace->fullscreen_toplevel = NULL;
    workspace->fullscreen_toplevel = toplevel;

    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, workspace->output->wlr_output, &output_box);
    toplevel_set_pending_state(toplevel, output_box.x, output_box.y,
                               output_box.width, output_box.height);

    if(toplevel->floating) {
      /* calculate where the toplevel should be placed after exiting fullscreen,
       * see note for floating bellow */
      uint32_t old_output_relative_x =
        toplevel->prev_geometry.x - old_workspace->output->usable_area.x;
      double relative_x =
        (double)old_output_relative_x / old_workspace->output->usable_area.width;

      uint32_t old_output_relative_y =
        toplevel->prev_geometry.y - old_workspace->output->usable_area.y;
      double relative_y =
        (double)old_output_relative_y / old_workspace->output->usable_area.height;

      uint32_t new_output_x = workspace->output->usable_area.x
        + relative_x * workspace->output->usable_area.width;
      uint32_t new_output_y = workspace->output->usable_area.y
        + relative_y * workspace->output->usable_area.height;

      toplevel->prev_geometry.x = new_output_x;
      toplevel->prev_geometry.y = new_output_y;
    } else {
      layout_set_pending_state(old_workspace);
    }
  } else if(toplevel->floating && old_workspace->output != workspace->output) {
    /* we want to place the toplevel to the same relative coordinates,
     * as the new output may have a different resolution */
    uint32_t old_output_relative_x =
      toplevel->scene_tree->node.x - old_workspace->output->usable_area.x;
    double relative_x =
      (double)old_output_relative_x / old_workspace->output->usable_area.width;

    uint32_t old_output_relative_y =
      toplevel->scene_tree->node.y - old_workspace->output->usable_area.y;
    double relative_y =
      (double)old_output_relative_y / old_workspace->output->usable_area.height;

    uint32_t new_output_x = workspace->output->usable_area.x
      + relative_x * workspace->output->usable_area.width;
    uint32_t new_output_y = workspace->output->usable_area.y
      + relative_y * workspace->output->usable_area.height;

    toplevel_set_pending_state(toplevel, new_output_x, new_output_y,
                               toplevel->current.width, toplevel->current.height);
  } else {
    layout_set_pending_state(old_workspace);
    layout_set_pending_state(workspace);
  }

  /* change active workspace */
  change_workspace(workspace, true);
}
