#include <pixman.h>
#include <stdint.h>
#include <wlr/types/wlr_buffer.h>

struct pixman_buffer {
    struct wlr_buffer base;
    pixman_image_t *image;

    int width, height;
};

// creates a new buffer backed by a pixman image. this buffer should not be destroyed manually, but by dropping the
// appropriate wlr_buffer (`pixman_buffer->base`)
struct pixman_buffer *
pixman_buffer_create(int width, int height);
