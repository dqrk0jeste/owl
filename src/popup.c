#include "popup.h"

#include "owl.h"
#include "something.h"
#include "toplevel.h"
#include "workspace.h"
#include "layer_surface.h"

#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_layout.h>

extern struct owl_server server;

void
server_handle_new_popup(struct wl_listener *listener, void *data) {
  /* this event is raised when a client creates a new popup */
  struct wlr_xdg_popup *xdg_popup = data;

  struct owl_popup *popup = calloc(1, sizeof(*popup));
  popup->xdg_popup = xdg_popup;
  
  popup->something.type = OWL_POPUP;
  popup->something.popup = popup;

  if(xdg_popup->parent != NULL) {
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    struct wlr_scene_tree *parent_tree = parent->data;
    popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    xdg_popup->base->data = popup->scene_tree;
    popup->scene_tree->node.data = &popup->something;
  } else {
    /* if there is no parent, than we keep the reference to our owl_popup state in this */
    /* user data pointer, in order to later reparent this popup (see layer_surface_handle_new_popup) */
    xdg_popup->base->data = popup;
  }

  popup->commit.notify = xdg_popup_handle_commit;
  wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

  popup->destroy.notify = xdg_popup_handle_destroy;
  wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void
xdg_popup_handle_commit(struct wl_listener *listener, void *data) {
  struct owl_popup *popup = wl_container_of(listener, popup, commit);

  if(!popup->xdg_popup->base->initialized) return;

  if(popup->xdg_popup->base->initial_commit) {
    struct owl_something *root = root_parent_of_surface(popup->xdg_popup->base->surface);

    if(root == NULL) {
      wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    } else if(root->type == OWL_TOPLEVEL) {
      struct wlr_box output_box = root->toplevel->workspace->output->usable_area;

      output_box.x -= root->toplevel->scene_tree->node.x;
      output_box.y -= root->toplevel->scene_tree->node.y;

      wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &output_box);
    } else {
      struct owl_layer_surface *layer_surface= root->layer_surface;
      struct wlr_output *wlr_output = layer_surface->wlr_layer_surface->output;

      struct wlr_box output_box;
      wlr_output_layout_get_box(server.output_layout, wlr_output, &output_box);

      output_box.x -= layer_surface->scene->tree->node.x;
      output_box.y -= layer_surface->scene->tree->node.y;

      wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &output_box);
    }
  }
}

void
xdg_popup_handle_destroy(struct wl_listener *listener, void *data) {
  struct owl_popup *popup = wl_container_of(listener, popup, destroy);

  wl_list_remove(&popup->commit.link);
  wl_list_remove(&popup->destroy.link);

  free(popup);
}
