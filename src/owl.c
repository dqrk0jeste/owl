#include "owl.h"

#include "helpers.h"
#include "ipc.h"
#include "keyboard.h"
#include "config.h"
#include "output.h"
#include "toplevel.h"
#include "popup.h"
#include "layer_surface.h"
#include "decoration.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <wayland-util.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_seat.h"
#include "wlr/types/wlr_cursor.h"
#include "wlr/types/wlr_data_device.h"
#include "wlr/backend.h"
#include "wlr/render/allocator.h"
#include "wlr/types/wlr_compositor.h"
#include "wlr/types/wlr_subcompositor.h"
#include "wlr/types/wlr_subcompositor.h"
#include "wlr/types/wlr_xdg_output_v1.h"
#include "wlr/types/wlr_xcursor_manager.h"
#include "wlr/types/wlr_xdg_decoration_v1.h"
#include "wlr/types/wlr_data_control_v1.h"
#include "wlr/types/wlr_screencopy_v1.h"
#include "wlr/types/wlr_viewporter.h"
#include "wlr/types/wlr_foreign_toplevel_management_v1.h"

/* we initialize an instance of our global state */
struct owl_server server;

/* handles child processes */
static void
sigchld_handler(int signo) {
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

static void
server_handle_new_input(struct wl_listener *listener, void *data) {
  struct wlr_input_device *input = data;

  switch(input->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
      server_handle_new_keyboard(input);
      break;
    case WLR_INPUT_DEVICE_POINTER:
      server_handle_new_pointer(input);
      break;
    default:
      /* owl doesnt support touch devices, drawing tablets etc */
      break;
  }

  /* we need to let the wlr_seat know what our capabilities are, which is
   * communiciated to the client. we always have a cursor, even if
   * there are no pointer devices, so we always include that capability. */
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty(&server.keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(server.seat, caps);
}

static void 
server_handle_request_cursor(struct wl_listener *listener, void *data) {
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  struct wlr_seat_client *focused_client = server.seat->pointer_state.focused_client;
  if(focused_client == event->seat_client) {
    /* once we've vetted the client, we can tell the cursor to use the
     * provided surface as the cursor image. it will set the hardware cursor
     * on the output that it's currently on and continue to do so as the
     * cursor moves between outputs */
    wlr_cursor_set_surface(server.cursor, event->surface,
                           event->hotspot_x, event->hotspot_y);
    /* TODO: maybe this should be placed elsewhere */
    server.client_cursor.surface = event->surface;
    server.client_cursor.hotspot_x = event->hotspot_x;
    server.client_cursor.hotspot_y = event->hotspot_y;
  }
}

static void
server_handle_request_set_selection(struct wl_listener *listener, void *data) {
  /* this event is raised by the seat when a client wants to set the selection,
   * usually when the user copies something. wlroots allows compositors to
   * ignore such requests if they so choose, but in owl we always honor
   */
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(server.seat, event->source, event->serial);
}

int
main(int argc, char *argv[]) {
  /* this is ripped straight from chatgpt, it prevents the creation of zombie processes. */
  struct sigaction sa;
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sa, NULL);

  bool debug = false;
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--debug") == 0) {
      debug = true;
    }
  }

  mkdir("/tmp/owl", 0777);
  if(debug) {
    /* make it so all the logs do to the log file */
    FILE *logs = fopen("/tmp/owl/logs", "w");
    if(logs != NULL) {
      int fd = fileno(logs);
      close(1);
      close(2);
      dup2(fd, 1);
      dup2(fd, 2);
      fclose(logs);
    }

    wlr_log_init(WLR_DEBUG, NULL);
  } else {
    wlr_log_init(WLR_INFO, NULL);
  }

  bool valid_config = server_load_config();
  if(!valid_config) {
    wlr_log(WLR_ERROR, "there is a problem in the config, quiting");
    return 1;
  }

  /* The Wayland display is managed by libwayland. It handles accepting
   * clients from the Unix socket, manging Wayland globals, and so on. */
  server.wl_display = wl_display_create();
  server.wl_event_loop = wl_display_get_event_loop(server.wl_display);

  /* The backend is a wlroots feature which abstracts the underlying input and
   * output hardware. The autocreate option will choose the most suitable
   * backend based on the current environment, such as opening an X11 window
   * if an X11 server is running. */
  server.backend = wlr_backend_autocreate(server.wl_event_loop, NULL);
  if(server.backend == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_backend");
    return 1;
  }

  /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
   * can also specify a renderer using the WLR_RENDERER env var.
   * The renderer is responsible for defining the various pixel formats it
   * supports for shared memory, this configures that for clients. */
  server.renderer = wlr_renderer_autocreate(server.backend);
  if(server.renderer == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_renderer");
    return 1;
  }

  wlr_renderer_init_wl_display(server.renderer, server.wl_display);

  /* Autocreates an allocator for us.
   * The allocator is the bridge between the renderer and the backend. It
   * handles the buffer creation, allowing wlroots to render onto the
   * screen */
  server.allocator = wlr_allocator_autocreate(server.backend,
                                              server.renderer);
  if(server.allocator == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_allocator");
    return 1;
  }

  /* This creates some hands-off wlroots interfaces. The compositor is
   * necessary for clients to allocate surfaces, the subcompositor allows to
   * assign the role of subsurfaces to surfaces and the data device manager
   * handles the clipboard. Each of these wlroots interfaces has room for you
   * to dig your fingers in and play with their behavior if you want. Note that
   * the clients cannot set the selection directly without compositor approval,
   * see the handling of the request_set_selection event below.*/
  wlr_compositor_create(server.wl_display, 5, server.renderer);
  wlr_subcompositor_create(server.wl_display);
  wlr_data_device_manager_create(server.wl_display);

  /* Creates an output layout, which a wlroots utility for working with an
   * arrangement of screens in a physical layout. */
  server.output_layout = wlr_output_layout_create(server.wl_display);

  /* Configure a listener to be notified when new outputs are available on the
   * backend. */
  wl_list_init(&server.outputs);
  server.new_output.notify = server_handle_new_output;
  wl_signal_add(&server.backend->events.new_output, &server.new_output);

  /* create a manager used for comunicating with the clients */
  server.xdg_output_manager = wlr_xdg_output_manager_v1_create(server.wl_display,
                                                               server.output_layout);

  /* Create a scene graph. This is a wlroots abstraction that handles all
   * rendering and damage tracking. All the compositor author needs to do
   * is add things that should be rendered to the scene graph at the proper
   * positions and then call wlr_scene_output_commit() to render a frame if
   * necessary.
   */
  server.scene = wlr_scene_create();
  server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

  /* create all the scenes in the correct order */
  server.background_tree = wlr_scene_tree_create(&server.scene->tree);
  server.bottom_tree = wlr_scene_tree_create(&server.scene->tree);
  server.tiled_tree = wlr_scene_tree_create(&server.scene->tree);
  server.floating_tree = wlr_scene_tree_create(&server.scene->tree);
  server.top_tree = wlr_scene_tree_create(&server.scene->tree);
  server.fullscreen_tree = wlr_scene_tree_create(&server.scene->tree);
  server.overlay_tree = wlr_scene_tree_create(&server.scene->tree);

  /* Set up xdg-shell version 6. The xdg-shell is a Wayland protocol which is
   * used for application windows. For more detail on shells, refer to
   * https://drewdevault.com/2018/07/29/Wayland-shells.html.
   */
  server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 6);
  server.new_xdg_toplevel.notify = server_handle_new_toplevel;
  wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
  server.new_xdg_popup.notify = server_handle_new_popup;
  wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

  server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 5);
  server.new_layer_surface.notify = server_handle_new_layer_surface;
  server.layer_shell->data = &server;
  wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

  /*
   * Creates a cursor, which is a wlroots utility for tracking the cursor
   * image shown on screen.
   */
  server.cursor = wlr_cursor_create();
  wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

  /* Creates an xcursor manager, another wlroots utility which loads up
   * Xcursor themes to source cursor images from and makes sure that cursor
   * images are available at all scale factors on the screen (necessary for
   * HiDPI support). */
  server.cursor_mgr = wlr_xcursor_manager_create(server.config->cursor_theme, server.config->cursor_size);

  /*
   * wlr_cursor *only* displays an image on screen. It does not move around
   * when the pointer moves. However, we can attach input devices to it, and
   * it will generate aggregate events for all of them. In these events, we
   * can choose how we want to process them, forwarding them to clients and
   * moving the cursor around. More detail on this process is described in
   * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html.
   *
   * And more comments are sprinkled throughout the notify functions above.
   */

  server.cursor_mode = OWL_CURSOR_PASSTHROUGH;
  server.cursor_motion.notify = server_handle_cursor_motion;
  wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
  server.cursor_motion_absolute.notify = server_handle_cursor_motion_absolute;
  wl_signal_add(&server.cursor->events.motion_absolute,
                &server.cursor_motion_absolute);
  server.cursor_button.notify = server_handle_cursor_button;
  wl_signal_add(&server.cursor->events.button, &server.cursor_button);
  server.cursor_axis.notify = server_handle_cursor_axis;
  wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
  server.cursor_frame.notify = server_handle_cursor_frame;
  wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

  /*
   * Configures a seat, which is a single "seat" at which a user sits and
   * operates the computer. This conceptually includes up to one keyboard,
   * pointer, touch, and drawing tablet device. We also rig up a listener to
   * let us know when new input devices are available on the backend.
   */
  wl_list_init(&server.keyboards);
  server.new_input.notify = server_handle_new_input;
  wl_signal_add(&server.backend->events.new_input, &server.new_input);
  server.seat = wlr_seat_create(server.wl_display, "seat0");
  server.request_cursor.notify = server_handle_request_cursor;
  wl_signal_add(&server.seat->events.request_set_cursor,
                &server.request_cursor);
  server.request_set_selection.notify = server_handle_request_set_selection;
  wl_signal_add(&server.seat->events.request_set_selection,
                &server.request_set_selection);

  /* handles clipboard clients */
  server.data_control_manager = wlr_data_control_manager_v1_create(server.wl_display);

  /* configures decorations */
  server.xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server.wl_display);

  server.request_xdg_decoration.notify = server_handle_request_xdg_decoration;
  wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration,
                &server.request_xdg_decoration);

  server.viewporter = wlr_viewporter_create(server.wl_display);

  server.screencopy_manager = wlr_screencopy_manager_v1_create(server.wl_display);
  server.foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(server.wl_display);

  /* Add a Unix socket to the Wayland display. */
  const char *socket = wl_display_add_socket_auto(server.wl_display);
  if (!socket) {
    wlr_backend_destroy(server.backend);
    return 1;
  }

  /* Start the backend. This will enumerate outputs and inputs, become the DRM
   * master, etc */
  if(!wlr_backend_start(server.backend)) {
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    return 1;
  }

  /* Set the WAYLAND_DISPLAY environment variable to our socket */
  setenv("WAYLAND_DISPLAY", socket, true);

  /* creating a thread for the ipc to run on */
  pthread_t thread_id;
  pthread_create(&thread_id, NULL, run_ipc, NULL);

  /* sleep a bit so the ipc starts, 0.1 seconds is probably enough */
  usleep(100000);

  for(size_t i = 0; i < server.config->run_count; i++) {
    run_cmd(server.config->run[i]);
  }

  /* run the wayland event loop. */
  wlr_log(WLR_INFO, "running owl on WAYLAND_DISPLAY=%s", socket);
  wl_display_run(server.wl_display);

  /* Once wl_display_run returns, we destroy all clients then shut down the
   * server. */
  wl_display_destroy_clients(server.wl_display);
  wlr_scene_node_destroy(&server.scene->tree.node);
  wlr_xcursor_manager_destroy(server.cursor_mgr);
  wlr_cursor_destroy(server.cursor);
  wlr_allocator_destroy(server.allocator);
  wlr_renderer_destroy(server.renderer);
  wlr_backend_destroy(server.backend);
  wl_display_destroy(server.wl_display);
  return 0;
}
