#ifndef MINARCH_HW_VIDEO_H
#define MINARCH_HW_VIDEO_H

#include <stdbool.h>
#include "libretro.h"

typedef struct {
    bool enabled;
    bool frame_ready;
    bool context_reset_pending;
    enum retro_hw_context_type context_type;
    unsigned version_major;
    unsigned version_minor;
    unsigned width;
    unsigned height;
    struct retro_hw_render_callback callback;
} MinarchHWVideo;

void MinarchHWVideo_reset(MinarchHWVideo *video);
bool MinarchHWVideo_accept_request(MinarchHWVideo *video, const struct retro_hw_render_callback *request);
bool MinarchHWVideo_prepare_request(
    MinarchHWVideo *video,
    struct retro_hw_render_callback *request,
    retro_hw_get_current_framebuffer_t get_current_framebuffer,
    retro_hw_get_proc_address_t get_proc_address);
void MinarchHWVideo_clear_context_reset_pending(MinarchHWVideo *video);
bool MinarchHWVideo_take_context_reset_pending(MinarchHWVideo *video);
void MinarchHWVideo_record_frame(MinarchHWVideo *video, unsigned width, unsigned height);
void MinarchHWVideo_invalidate_frame(MinarchHWVideo *video);

#endif
