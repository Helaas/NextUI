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
    video->context_reset_pending = true;
    video->context_type = request->context_type;
    video->version_major = request->version_major;
    video->version_minor = request->version_minor;
    video->callback = *request;
    return true;
}

bool MinarchHWVideo_prepare_request(
    MinarchHWVideo *video,
    struct retro_hw_render_callback *request,
    retro_hw_get_current_framebuffer_t get_current_framebuffer,
    retro_hw_get_proc_address_t get_proc_address)
{
    struct retro_hw_render_callback prepared;

    if (!request) {
        return false;
    }

    prepared = *request;
    prepared.get_current_framebuffer = get_current_framebuffer;
    prepared.get_proc_address = get_proc_address;

    if (prepared.context_type == RETRO_HW_CONTEXT_OPENGLES3 &&
        prepared.version_major == 0 &&
        prepared.version_minor == 0) {
        prepared.version_major = 3;
        prepared.version_minor = 0;
    }

    if (!MinarchHWVideo_accept_request(video, &prepared)) {
        return false;
    }

    *request = prepared;
    return true;
}

void MinarchHWVideo_clear_context_reset_pending(MinarchHWVideo *video)
{
    if (!video) {
        return;
    }

    video->context_reset_pending = false;
}

bool MinarchHWVideo_take_context_reset_pending(MinarchHWVideo *video)
{
    bool pending = video->context_reset_pending;
    video->context_reset_pending = false;
    return pending;
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
