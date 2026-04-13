#ifndef MINARCH_HW_VIDEO_H
#define MINARCH_HW_VIDEO_H

#include <stdbool.h>
#include "libretro.h"

typedef struct {
    bool enabled;
    bool frame_ready;
    enum retro_hw_context_type context_type;
    unsigned version_major;
    unsigned version_minor;
    unsigned width;
    unsigned height;
    struct retro_hw_render_callback callback;
} MinarchHWVideo;

void MinarchHWVideo_reset(MinarchHWVideo *video);
bool MinarchHWVideo_accept_request(MinarchHWVideo *video, const struct retro_hw_render_callback *request);
void MinarchHWVideo_record_frame(MinarchHWVideo *video, unsigned width, unsigned height);
void MinarchHWVideo_invalidate_frame(MinarchHWVideo *video);

#endif
