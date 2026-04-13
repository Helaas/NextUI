#include <assert.h>
#include <string.h>

#include "../hw_video.h"

bool MinarchHWVideo_take_context_reset_pending(MinarchHWVideo *video);
bool MinarchHWVideo_prepare_request(
    MinarchHWVideo *video,
    struct retro_hw_render_callback *request,
    retro_hw_get_current_framebuffer_t get_current_framebuffer,
    retro_hw_get_proc_address_t get_proc_address);
void MinarchHWVideo_clear_context_reset_pending(MinarchHWVideo *video);

static void dummy_context_reset(void) {}
static void dummy_context_destroy(void) {}
static uintptr_t dummy_get_current_framebuffer(void) { return 1234; }
static retro_proc_address_t dummy_get_proc_address(const char *name)
{
    (void)name;
    return (retro_proc_address_t)dummy_context_reset;
}

static void test_rejects_non_gles_context(void)
{
    MinarchHWVideo video;
    MinarchHWVideo_reset(&video);

    struct retro_hw_render_callback request = {0};
    request.context_type = RETRO_HW_CONTEXT_VULKAN;
    request.version_major = 1;
    request.version_minor = 0;

    assert(!MinarchHWVideo_accept_request(&video, &request));
    assert(!video.enabled);
}

static void test_accepts_gles3_request_and_copies_callback(void)
{
    MinarchHWVideo video;
    MinarchHWVideo_reset(&video);

    struct retro_hw_render_callback request = {0};
    request.context_type = RETRO_HW_CONTEXT_OPENGLES3;
    request.version_major = 3;
    request.version_minor = 0;
    request.depth = true;
    request.stencil = true;
    request.bottom_left_origin = true;
    request.context_reset = dummy_context_reset;
    request.context_destroy = dummy_context_destroy;

    assert(MinarchHWVideo_accept_request(&video, &request));
    assert(video.enabled);
    assert(video.context_type == RETRO_HW_CONTEXT_OPENGLES3);
    assert(video.version_major == 3);
    assert(video.version_minor == 0);
    assert(video.callback.depth);
    assert(video.callback.stencil);
    assert(video.callback.bottom_left_origin);
    assert(video.callback.context_reset == dummy_context_reset);
    assert(video.callback.context_destroy == dummy_context_destroy);
}

static void test_frame_record_and_invalidation(void)
{
    MinarchHWVideo video;
    MinarchHWVideo_reset(&video);

    struct retro_hw_render_callback request = {0};
    request.context_type = RETRO_HW_CONTEXT_OPENGLES2;
    request.version_major = 2;
    request.version_minor = 0;

    assert(MinarchHWVideo_accept_request(&video, &request));

    MinarchHWVideo_record_frame(&video, 640, 480);
    assert(video.frame_ready);
    assert(video.width == 640);
    assert(video.height == 480);

    MinarchHWVideo_invalidate_frame(&video);
    assert(!video.frame_ready);
    assert(video.width == 640);
    assert(video.height == 480);
}

static void test_rejected_request_clears_previous_state(void)
{
    MinarchHWVideo video;
    MinarchHWVideo_reset(&video);

    struct retro_hw_render_callback accepted = {0};
    accepted.context_type = RETRO_HW_CONTEXT_OPENGLES2;
    accepted.version_major = 2;
    accepted.version_minor = 0;

    assert(MinarchHWVideo_accept_request(&video, &accepted));
    MinarchHWVideo_record_frame(&video, 320, 240);
    assert(video.enabled);
    assert(video.frame_ready);
    assert(video.context_type == RETRO_HW_CONTEXT_OPENGLES2);

    struct retro_hw_render_callback rejected = {0};
    rejected.context_type = RETRO_HW_CONTEXT_VULKAN;
    rejected.version_major = 1;
    rejected.version_minor = 0;

    assert(!MinarchHWVideo_accept_request(&video, &rejected));
    assert(!video.enabled);
    assert(!video.frame_ready);
    assert(video.context_type == 0);
    assert(video.version_major == 0);
    assert(video.version_minor == 0);
    assert(video.width == 0);
    assert(video.height == 0);
}

static void test_context_reset_is_pending_until_consumed(void)
{
    MinarchHWVideo video;
    MinarchHWVideo_reset(&video);

    struct retro_hw_render_callback request = {0};
    request.context_type = RETRO_HW_CONTEXT_OPENGLES2;
    request.version_major = 2;
    request.version_minor = 0;
    request.context_reset = dummy_context_reset;

    assert(MinarchHWVideo_accept_request(&video, &request));
    assert(MinarchHWVideo_take_context_reset_pending(&video));
    assert(!MinarchHWVideo_take_context_reset_pending(&video));

    MinarchHWVideo_reset(&video);
    assert(!MinarchHWVideo_take_context_reset_pending(&video));
}

static void test_prepare_request_updates_core_callbacks_in_place(void)
{
    MinarchHWVideo video;
    MinarchHWVideo_reset(&video);

    struct retro_hw_render_callback request = {0};
    request.context_type = RETRO_HW_CONTEXT_OPENGLES3;
    request.context_reset = dummy_context_reset;
    request.context_destroy = dummy_context_destroy;

    assert(MinarchHWVideo_prepare_request(
        &video,
        &request,
        dummy_get_current_framebuffer,
        dummy_get_proc_address));
    assert(request.version_major == 3);
    assert(request.version_minor == 0);
    assert(request.get_current_framebuffer == dummy_get_current_framebuffer);
    assert(request.get_proc_address == dummy_get_proc_address);
    assert(video.callback.get_current_framebuffer == dummy_get_current_framebuffer);
    assert(video.callback.get_proc_address == dummy_get_proc_address);
}

static void test_clear_context_reset_pending(void)
{
    MinarchHWVideo video;
    MinarchHWVideo_reset(&video);

    struct retro_hw_render_callback request = {0};
    request.context_type = RETRO_HW_CONTEXT_OPENGLES2;

    assert(MinarchHWVideo_accept_request(&video, &request));
    MinarchHWVideo_clear_context_reset_pending(&video);
    assert(!MinarchHWVideo_take_context_reset_pending(&video));
}

int main(void)
{
    test_rejects_non_gles_context();
    test_accepts_gles3_request_and_copies_callback();
    test_frame_record_and_invalidation();
    test_rejected_request_clears_previous_state();
    test_context_reset_is_pending_until_consumed();
    test_prepare_request_updates_core_callbacks_in_place();
    test_clear_context_reset_pending();
    return 0;
}
