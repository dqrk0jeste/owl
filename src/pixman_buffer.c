#include "pixman_buffer.h"

#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>

#include "drm_fourcc.h"

static void
pixman_buffer_handle_destroy(struct wlr_buffer *wlr_buffer) {
    struct pixman_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);

    pixman_image_unref(buffer->image);
    free(buffer);
}

static bool
pixman_buffer_handle_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags, void **data, uint32_t *format,
        size_t *stride) {
    struct pixman_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);

    *data = pixman_image_get_data(buffer->image);
    *stride = pixman_image_get_stride(buffer->image);
    *format = DRM_FORMAT_ARGB8888;

    return true;
}

static void
pixman_buffer_handle_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
    // do nothing
}

static const struct wlr_buffer_impl pixman_buffer_impl = {
        .destroy = pixman_buffer_handle_destroy,
        .begin_data_ptr_access = pixman_buffer_handle_begin_data_ptr_access,
        .end_data_ptr_access = pixman_buffer_handle_end_data_ptr_access,
};

struct pixman_buffer *
pixman_buffer_create(int width, int height) {
    struct pixman_buffer *buffer = calloc(1, sizeof(*buffer));

    wlr_buffer_init(&buffer->base, &pixman_buffer_impl, width, height);

    buffer->width = width;
    buffer->height = height;
    buffer->image = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, NULL, 0);

    pixman_region32_t clip;
    pixman_region32_init_rect(&clip, 0, 0, width, height);
    pixman_image_set_clip_region32(buffer->image, &clip);
    pixman_region32_fini(&clip);

    return buffer;
}
