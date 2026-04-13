#include <string.h>

#include "hw_video.h"

void MinarchHWVideo_reset(MinarchHWVideo *video)
{
    memset(video, 0, sizeof(*video));
}

bool MinarchHWVideo_accept_request(MinarchHWVideo *video, const struct retro_hw_render_callback *request)
{
    MinarchHWVideo_reset(video);

    if (!request) {
        return false;
    }

    if (request->context_type != RETRO_HW_CONTEXT_OPENGLES2 &&
        request->context_type != RETRO_HW_CONTEXT_OPENGLES3) {
        return false;
    }

    video->enabled = true;
    video->frame_ready = false;
    video->context_type = request->context_type;
    video->version_major = request->version_major;
    video->version_minor = request->version_minor;
    video->callback = *request;
    return true;
}

void MinarchHWVideo_record_frame(MinarchHWVideo *video, unsigned width, unsigned height)
{
    video->frame_ready = true;
    video->width = width;
    video->height = height;
}

void MinarchHWVideo_invalidate_frame(MinarchHWVideo *video)
{
    video->frame_ready = false;
}
