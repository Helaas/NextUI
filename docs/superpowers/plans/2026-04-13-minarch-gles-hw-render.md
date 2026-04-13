# Minarch GLES HW Render Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add GLES-only libretro hardware-render support to Minarch on `tg5040` and `tg5050` so `GL.pak`, `P64.pak`, and `FLYCAST.pak` run through the existing Minarch compositor with working menu screenshots, save/load, rewind, overlays, and frontend shaders.

**Architecture:** Keep one frontend-owned SDL GLES context and one final presentation path. Hardware-rendered cores render into a frontend-managed FBO/texture, Minarch treats `RETRO_HW_FRAME_BUFFER_VALID` as a GPU-backed source, and the existing GL compositor in `generic_video.c` remains the only code that scales, shaders, overlays, captures screenshots, and swaps the final frame.

**Tech Stack:** C99, SDL2, OpenGL ES via the existing SDL GL context, libretro frontend callbacks, existing NextUI/Minarch makefiles for `desktop`, `tg5040`, and `tg5050`

---

## File Structure

- `workspace/all/minarch/hw_video.h`
  - New small, testable helper for Minarch HW-render session state and GLES-only request validation.
- `workspace/all/minarch/hw_video.c`
  - New implementation file for the helper above.
- `workspace/all/minarch/tests/test_hw_video.c`
  - New host-side unit test for HW session state transitions and request validation.
- `workspace/all/minarch/minarch.c`
  - Existing Minarch integration point for libretro env callbacks, core lifecycle, frame submission, state invalidation, and menu/save/rewind behavior.
- `workspace/all/minarch/makefile`
  - Add `hw_video.c` to the Minarch build.
- `workspace/all/common/api.h`
  - Extend `GFX_Renderer` so the compositor can distinguish software pixels from a GL texture source. Declare the minimal frontend GL target helper functions Minarch needs.
- `workspace/all/common/generic_video.c`
  - Own the frontend-managed core FBO/texture, expose framebuffer/proc lookup helpers, and make `PLAT_GL_Swap()` consume either uploaded pixels or a hardware-rendered texture.

## Notes Before Starting

- There is no existing end-to-end automated test harness for Minarch/libretro integration in this repo.
- This plan adds one small unit-testable helper (`hw_video.c`) and relies on full builds plus manual smoke checks for the compositor integration.
- The approved acceptance criteria are the explicit ones from the design spec: booting the three pak repos plus menu/screenshots/save/load/rewind/overlays/frontend shaders. Do not expand scope to unrelated rendering cleanup.
- The current tree does not have a separate runtime GL-context-recreation hook beyond normal startup/shutdown. Keep lifecycle work scoped to startup, shutdown, AV-info-driven resize, and frame-validity invalidation instead of inventing a new reset subsystem.

### Task 1: Add a Small Unit-Tested HW Video Helper

**Files:**
- Create: `workspace/all/minarch/hw_video.h`
- Create: `workspace/all/minarch/hw_video.c`
- Create: `workspace/all/minarch/tests/test_hw_video.c`
- Modify: `workspace/all/minarch/makefile`

- [ ] **Step 1: Write the failing unit test**

Create `workspace/all/minarch/tests/test_hw_video.c`:

```c
#include <assert.h>
#include <string.h>

#include "../hw_video.h"

static void dummy_context_reset(void) {}
static void dummy_context_destroy(void) {}

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

int main(void)
{
    test_rejects_non_gles_context();
    test_accepts_gles3_request_and_copies_callback();
    test_frame_record_and_invalidation();
    return 0;
}
```

- [ ] **Step 2: Run the unit test command to verify it fails**

Run:

```bash
gcc -std=gnu99 \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/libretro-common/include \
  /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/tests/test_hw_video.c \
  /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/hw_video.c \
  -o /tmp/test_hw_video
```

Expected: FAIL with `fatal error` for `hw_video.h` or missing `hw_video.c`.

- [ ] **Step 3: Write the minimal helper and compile it into Minarch**

Create `workspace/all/minarch/hw_video.h`:

```c
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
```

Create `workspace/all/minarch/hw_video.c`:

```c
#include <string.h>

#include "hw_video.h"

void MinarchHWVideo_reset(MinarchHWVideo *video)
{
    memset(video, 0, sizeof(*video));
}

bool MinarchHWVideo_accept_request(MinarchHWVideo *video, const struct retro_hw_render_callback *request)
{
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
```

Modify `workspace/all/minarch/makefile`:

```make
SOURCE = $(TARGET).c hw_video.c ../common/scaler.c ../common/utils.c ../common/config.c ../common/api.c ../common/notification.c ../../$(PLATFORM)/platform/platform.c
```

- [ ] **Step 4: Run the unit test again and verify it passes**

Run:

```bash
gcc -std=gnu99 \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/libretro-common/include \
  /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/tests/test_hw_video.c \
  /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/hw_video.c \
  -o /tmp/test_hw_video && \
/tmp/test_hw_video
```

Expected: command exits `0` with no stdout/stderr.

- [ ] **Step 5: Commit**

```bash
git add \
  workspace/all/minarch/hw_video.h \
  workspace/all/minarch/hw_video.c \
  workspace/all/minarch/tests/test_hw_video.c \
  workspace/all/minarch/makefile
git commit -m "minarch: add HW video session helper"
```

### Task 2: Extend the Frontend Renderer Contract for HW Textures

**Files:**
- Modify: `workspace/all/common/api.h`
- Modify: `workspace/all/common/generic_video.c`

- [ ] **Step 1: Write a failing header-contract smoke test**

Create `workspace/all/minarch/tests/test_hw_renderer_contract.c`:

```c
#include <assert.h>
#include <stdint.h>

#include "../../common/api.h"

int main(void)
{
    GFX_Renderer renderer = {0};
    renderer.source_type = GFX_SOURCE_HW_TEXTURE;
    renderer.src_texture = 42;
    renderer.src_texture_flipped = 1;

    assert(renderer.source_type == GFX_SOURCE_HW_TEXTURE);
    assert(renderer.src_texture == 42);
    assert(renderer.src_texture_flipped == 1);
    return 0;
}
```

- [ ] **Step 2: Compile the header-contract smoke test and verify it fails**

Run:

```bash
gcc -std=gnu99 \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/common \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/desktop/platform \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/libretro-common/include \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/desktop/libmsettings \
  -c /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/tests/test_hw_renderer_contract.c \
  -o /tmp/test_hw_renderer_contract.o
```

Expected: FAIL because `GFX_SOURCE_HW_TEXTURE`, `source_type`, `src_texture`, and `src_texture_flipped` do not exist yet.

- [ ] **Step 3: Extend `GFX_Renderer` and declare the minimal GL target API**

Modify `workspace/all/common/api.h`:

```c
#include <stdint.h>

typedef enum {
    GFX_SOURCE_PIXELS = 0,
    GFX_SOURCE_HW_TEXTURE = 1,
} GFX_SourceType;

typedef struct GFX_Renderer {
    void* src;
    void* dst;
    void* blit;
    GFX_SourceType source_type;
    unsigned int src_texture;
    int src_texture_flipped;
    double aspect;
    int scale;
    int true_w;
    int true_h;
    int src_x;
    int src_y;
    int src_w;
    int src_h;
    int src_p;
    int dst_x;
    int dst_y;
    int dst_w;
    int dst_h;
    int dst_p;
} GFX_Renderer;

bool PLAT_coreVideoEnsureTarget(unsigned width, unsigned height, bool depth, bool stencil);
void PLAT_coreVideoDestroy(void);
uintptr_t PLAT_coreVideoFramebuffer(void);
void* PLAT_coreVideoProcAddress(const char *name);
unsigned int PLAT_coreVideoTexture(void);
```

- [ ] **Step 4: Add the core render target and texture-source branch to the compositor**

Modify `workspace/all/common/generic_video.c`:

```c
typedef struct {
    GLuint fbo;
    GLuint color_tex;
    GLuint depth_stencil_rbo;
    unsigned width;
    unsigned height;
    bool depth;
    bool stencil;
} CoreVideoTarget;

static CoreVideoTarget core_video = {0};

static void destroyCoreVideoTarget(void)
{
    if (core_video.depth_stencil_rbo) {
        glDeleteRenderbuffers(1, &core_video.depth_stencil_rbo);
    }
    if (core_video.color_tex) {
        glDeleteTextures(1, &core_video.color_tex);
    }
    if (core_video.fbo) {
        glDeleteFramebuffers(1, &core_video.fbo);
    }
    memset(&core_video, 0, sizeof(core_video));
}

bool PLAT_coreVideoEnsureTarget(unsigned width, unsigned height, bool depth, bool stencil)
{
    SDL_GL_MakeCurrent(vid.window, vid.gl_context);

    if (core_video.fbo &&
        core_video.width == width &&
        core_video.height == height &&
        core_video.depth == depth &&
        core_video.stencil == stencil) {
        return true;
    }

    destroyCoreVideoTarget();

    glGenFramebuffers(1, &core_video.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, core_video.fbo);

    glGenTextures(1, &core_video.color_tex);
    glBindTexture(GL_TEXTURE_2D, core_video.color_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, core_video.color_tex, 0);

    if (depth || stencil) {
        glGenRenderbuffers(1, &core_video.depth_stencil_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, core_video.depth_stencil_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, core_video.depth_stencil_rbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, core_video.depth_stencil_rbo);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroyCoreVideoTarget();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    core_video.width = width;
    core_video.height = height;
    core_video.depth = depth;
    core_video.stencil = stencil;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void PLAT_coreVideoDestroy(void)
{
    SDL_GL_MakeCurrent(vid.window, vid.gl_context);
    destroyCoreVideoTarget();
}

uintptr_t PLAT_coreVideoFramebuffer(void)
{
    return (uintptr_t)core_video.fbo;
}

void* PLAT_coreVideoProcAddress(const char *name)
{
    SDL_GL_MakeCurrent(vid.window, vid.gl_context);
    return SDL_GL_GetProcAddress(name);
}

unsigned int PLAT_coreVideoTexture(void)
{
    return core_video.color_tex;
}
```

Then update `runShaderPass()` and `PLAT_GL_Swap()` so the first shader/compositor input can come from either uploaded CPU pixels or `vid.blit->src_texture`. Keep the later passes unchanged.

Use this exact pattern in `runShaderPass()`:

```c
void runShaderPass(GLuint src_texture, GLuint shader_program, GLuint* target_texture,
                   int x, int y, int dst_width, int dst_height, Shader* shader,
                   int alpha, int filter, int flip_y)
{
    static GLuint static_VAO = 0, static_VBO = 0;
    static int last_flip_y = -1;

    if (static_VAO == 0 || last_flip_y != flip_y) {
        float v_top = flip_y ? 0.0f : 1.0f;
        float v_bottom = flip_y ? 1.0f : 0.0f;
        float vertices[] = {
            -1.0f,  1.0f, 0.0f, 1.0f,  0.0f, v_top,    0.0f, 0.0f,
            -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, v_bottom, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f,  1.0f, v_top,    0.0f, 0.0f,
             1.0f, -1.0f, 0.0f, 1.0f,  1.0f, v_bottom, 0.0f, 0.0f
        };

        if (static_VAO == 0) {
            glGenVertexArrays(1, &static_VAO);
            glGenBuffers(1, &static_VBO);
        }

        glBindVertexArray(static_VAO);
        glBindBuffer(GL_ARRAY_BUFFER, static_VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        last_flip_y = flip_y;
    }
}
```

And use this source-selection pattern in `PLAT_GL_Swap()`:

```c
if (vid.blit->source_type != GFX_SOURCE_HW_TEXTURE && !vid.blit->src) {
    return;
}

GLuint input_texture = 0;

if (vid.blit->source_type == GFX_SOURCE_HW_TEXTURE) {
    input_texture = vid.blit->src_texture;
    last_w = vid.blit->src_w;
    last_h = vid.blit->src_h;
} else {
    if (!src_texture || reloadShaderTextures) {
        if (src_texture == 0) {
            glGenTextures(1, &src_texture);
        }
        glBindTexture(GL_TEXTURE_2D, src_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nrofshaders > 0 ? shaders[0]->filter : finalScaleFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nrofshaders > 0 ? shaders[0]->filter : finalScaleFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindTexture(GL_TEXTURE_2D, src_texture);
    if (vid.blit->src_w != src_w_last || vid.blit->src_h != src_h_last || reloadShaderTextures) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vid.blit->src_w, vid.blit->src_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, vid.blit->src);
        src_w_last = vid.blit->src_w;
        src_h_last = vid.blit->src_h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vid.blit->src_w, vid.blit->src_h, GL_RGBA, GL_UNSIGNED_BYTE, vid.blit->src);
    }

    input_texture = src_texture;
    last_w = vid.blit->src_w;
    last_h = vid.blit->src_h;
}

runShaderPass(
    (i == 0) ? input_texture : shaders[i - 1]->texture,
    shaders[i]->shader_p ? shaders[i]->shader_p : g_noshader,
    &shaders[i]->texture,
    0, 0, dst_w, dst_h,
    shaders[i],
    0,
    (i == nrofshaders - 1) ? finalScaleFilter : shaders[i + 1]->filter,
    (i == 0) ? vid.blit->src_texture_flipped : 0
);
```

Also add `PLAT_coreVideoDestroy();` to `PLAT_quitVideo()` before the renderer/context are destroyed.

- [ ] **Step 5: Re-run the contract smoke test and a desktop compile**

Run:

```bash
gcc -std=gnu99 \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/common \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/desktop/platform \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/libretro-common/include \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/desktop/libmsettings \
  -c /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/tests/test_hw_renderer_contract.c \
  -o /tmp/test_hw_renderer_contract.o && \
make -C /Volumes/Storage/GitHub/NextUI_old/workspace PLATFORM=desktop all
```

Expected:

- header smoke test compiles successfully
- desktop build completes and produces `workspace/all/minarch/build/desktop/minarch.elf`

- [ ] **Step 6: Commit**

```bash
git add \
  workspace/all/common/api.h \
  workspace/all/common/generic_video.c \
  workspace/all/minarch/tests/test_hw_renderer_contract.c
git commit -m "common: let the compositor accept HW texture sources"
```

### Task 3: Wire Libretro HW Render Requests and Core Lifecycle into Minarch

**Files:**
- Modify: `workspace/all/minarch/minarch.c`

- [ ] **Step 1: Add the HW video state and frontend callback bridge**

In `workspace/all/minarch/minarch.c`, add:

```c
#include "hw_video.h"

static MinarchHWVideo hw_video = {0};

static uintptr_t hw_render_get_current_framebuffer(void)
{
    return PLAT_coreVideoFramebuffer();
}

static retro_proc_address_t hw_render_get_proc_address(const char *name)
{
    return (retro_proc_address_t)PLAT_coreVideoProcAddress(name);
}

static bool HWVideo_ensure_target(unsigned width, unsigned height)
{
    unsigned target_w = width ? width : 640;
    unsigned target_h = height ? height : 480;
    return PLAT_coreVideoEnsureTarget(target_w, target_h, hw_video.callback.depth, hw_video.callback.stencil);
}
```

- [ ] **Step 2: Replace the `SET_HW_RENDER` stub with a real GLES-only implementation**

Replace the current `RETRO_ENVIRONMENT_SET_HW_RENDER` case in `environment_callback()` with:

```c
case RETRO_ENVIRONMENT_SET_HW_RENDER: {
    const struct retro_hw_render_callback *request = (const struct retro_hw_render_callback *)data;
    struct retro_hw_render_callback prepared;

    if (!request) {
        return false;
    }

    prepared = *request;
    prepared.get_current_framebuffer = hw_render_get_current_framebuffer;
    prepared.get_proc_address = hw_render_get_proc_address;

    if (prepared.context_type == RETRO_HW_CONTEXT_OPENGLES3 &&
        prepared.version_major == 0 &&
        prepared.version_minor == 0) {
        prepared.version_major = 3;
        prepared.version_minor = 0;
    }

    if (!MinarchHWVideo_accept_request(&hw_video, &prepared)) {
        LOG_error("Unsupported HW render context type %d\n", request->context_type);
        return false;
    }

    if (!HWVideo_ensure_target(renderer.true_w, renderer.true_h)) {
        LOG_error("Failed to allocate frontend HW render target\n");
        MinarchHWVideo_reset(&hw_video);
        return false;
    }

    if (hw_video.callback.context_reset) {
        hw_video.callback.context_reset();
    }

    return true;
}
```

Add an explicit out-of-scope case:

```c
case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
    return false;
```

- [ ] **Step 3: Hook AV updates and shutdown into the frontend target lifecycle**

In `Core_updateAVInfo()`, after updating `core.aspect_ratio`, add:

```c
if (hw_video.enabled) {
    unsigned target_w = av_info.geometry.base_width ? av_info.geometry.base_width : av_info.geometry.max_width;
    unsigned target_h = av_info.geometry.base_height ? av_info.geometry.base_height : av_info.geometry.max_height;
    if (!HWVideo_ensure_target(target_w, target_h)) {
        LOG_error("Failed to resize frontend HW render target to %ux%u\n", target_w, target_h);
        quit = 1;
    }
}
```

In `Core_quit()`, before `core.unload_game();`, add:

```c
if (hw_video.enabled && hw_video.callback.context_destroy) {
    hw_video.callback.context_destroy();
}
PLAT_coreVideoDestroy();
MinarchHWVideo_reset(&hw_video);
```

- [ ] **Step 4: Build Minarch for desktop and both handheld targets**

Run:

```bash
make -C /Volumes/Storage/GitHub/NextUI_old/workspace PLATFORM=desktop all && \
make -C /Volumes/Storage/GitHub/NextUI_old/workspace PLATFORM=tg5040 all && \
make -C /Volumes/Storage/GitHub/NextUI_old/workspace PLATFORM=tg5050 all
```

Expected:

- `workspace/all/minarch/build/desktop/minarch.elf`
- `workspace/all/minarch/build/tg5040/minarch.elf`
- `workspace/all/minarch/build/tg5050/minarch.elf`

all exist, and the build exits `0`.

- [ ] **Step 5: Commit**

```bash
git add workspace/all/minarch/minarch.c
git commit -m "minarch: wire GLES HW render requests into core lifecycle"
```

### Task 4: Route HW Frames Through the Existing Compositor and Invalidate Stale GPU Frames

**Files:**
- Modify: `workspace/all/minarch/minarch.c`
- Modify: `workspace/all/common/generic_video.c`

- [ ] **Step 1: Split presentation so Minarch can present either a CPU frame or a GPU texture**

In `workspace/all/minarch/minarch.c`, replace `video_refresh_callback_main()` with a source-aware presenter:

```c
static void present_frame(const void *rgba_frame, unsigned width, unsigned height, size_t pitch, bool is_hw_frame)
{
    static uint32_t last_flip_time = 0;
    static int frame_counter = 0;
    const int max_frames = 8;

    Special_render();

    if (fast_forward && SDL_GetTicks() - last_flip_time < 10) {
        return;
    }

    if (renderer.dst_p == 0 || width != renderer.true_w || height != renderer.true_h) {
        selectScaler(width, height, width * (int)sizeof(uint32_t));
        GFX_clearAll();
        if (!shader_reset_suppressed) {
            GFX_resetShaders();
        } else {
            shader_reset_suppressed = 0;
        }
    }

    if (!is_hw_frame) {
        drawDebugHud(rgba_frame, width, height, pitch, fmt);
        if (frame_counter < 9) {
            applyFadeIn((uint32_t **)&rgba_frame, pitch, width, height, &frame_counter, max_frames);
        }
        renderer.source_type = GFX_SOURCE_PIXELS;
        renderer.src = (void *)rgba_frame;
        renderer.src_texture = 0;
        renderer.src_texture_flipped = 0;
    } else {
        renderer.source_type = GFX_SOURCE_HW_TEXTURE;
        renderer.src = NULL;
        renderer.src_texture = PLAT_coreVideoTexture();
        renderer.src_texture_flipped = hw_video.callback.bottom_left_origin ? 1 : 0;
    }

    renderer.dst = screen->pixels;
    GFX_blitRenderer(&renderer);
    screen_flip(screen);
    last_flip_time = SDL_GetTicks();
}
```

- [ ] **Step 2: Make `video_refresh_callback()` understand `RETRO_HW_FRAME_BUFFER_VALID` and dupe frames**

Replace the top of `video_refresh_callback()` with:

```c
static void video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (quit) {
        return;
    }

    if (hw_video.enabled && (data == RETRO_HW_FRAME_BUFFER_VALID || data == NULL)) {
        if (data == RETRO_HW_FRAME_BUFFER_VALID) {
            MinarchHWVideo_record_frame(&hw_video, width, height);
        } else if (!hw_video.frame_ready) {
            return;
        } else {
            width = hw_video.width;
            height = hw_video.height;
        }

        present_frame(NULL, width, height, width * sizeof(uint32_t), true);
        return;
    }

    /* Existing software conversion path stays below this point. */
```

And replace the final software present call with:

```c
present_frame(data, width, height, pitch, false);
```

- [ ] **Step 3: Invalidate stale HW frames in one shared rewind/save-state hook**

In `Rewind_on_state_change()`, add:

```c
if (hw_video.enabled) {
    MinarchHWVideo_invalidate_frame(&hw_video);
}
```

In `Core_reset()`, add the same invalidation before `core.reset();`:

```c
if (hw_video.enabled) {
    MinarchHWVideo_invalidate_frame(&hw_video);
}
```

This single shared hook covers:

- state load
- resume/autoload
- rewind re-seed
- explicit reset

because those code paths already call `Rewind_on_state_change()` or `Core_reset()`.

- [ ] **Step 4: Re-run the unit test and all three builds**

Run:

```bash
gcc -std=gnu99 \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch \
  -I/Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/libretro-common/include \
  /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/tests/test_hw_video.c \
  /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/hw_video.c \
  -o /tmp/test_hw_video && \
/tmp/test_hw_video && \
make -C /Volumes/Storage/GitHub/NextUI_old/workspace PLATFORM=desktop all && \
make -C /Volumes/Storage/GitHub/NextUI_old/workspace PLATFORM=tg5040 all && \
make -C /Volumes/Storage/GitHub/NextUI_old/workspace PLATFORM=tg5050 all
```

Expected: the unit test exits `0` and all three builds complete successfully.

- [ ] **Step 5: Commit**

```bash
git add \
  workspace/all/minarch/minarch.c \
  workspace/all/common/generic_video.c
git commit -m "minarch: present HW render frames through the existing compositor"
```

### Task 5: Verify Against the Three Pak Repos and a Software Regression

**Files:**
- Modify: `build/PAYLOAD/.system/tg5040/bin/minarch.elf`
- Modify: `build/PAYLOAD/.system/tg5050/bin/minarch.elf`

- [ ] **Step 1: Copy the freshly built Minarch binaries into the payload tree**

Run:

```bash
cp /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/build/tg5040/minarch.elf \
   /Volumes/Storage/GitHub/NextUI_old/build/PAYLOAD/.system/tg5040/bin/minarch.elf && \
cp /Volumes/Storage/GitHub/NextUI_old/workspace/all/minarch/build/tg5050/minarch.elf \
   /Volumes/Storage/GitHub/NextUI_old/build/PAYLOAD/.system/tg5050/bin/minarch.elf
```

Expected: both copy commands exit `0`.

- [ ] **Step 2: Build the three external pak repos for both handheld targets**

Run:

```bash
make -C /Volumes/Storage/GitHub/nextui-libretro-gl-core-pak package-tg5040 package-tg5050 && \
make -C /Volumes/Storage/GitHub/nextui-p64-pak package-tg5040 package-tg5050 && \
make -C /Volumes/Storage/GitHub/nextui-flycast-pak package-tg5040 package-tg5050
```

Expected:

- `nextui-libretro-gl-core-pak/build/tg5040/GL.pak`
- `nextui-libretro-gl-core-pak/build/tg5050/GL.pak`
- `nextui-p64-pak/build/tg5040/P64.pak`
- `nextui-p64-pak/build/tg5050/P64.pak`
- `nextui-flycast-pak/build/tg5040/FLYCAST.pak`
- `nextui-flycast-pak/build/tg5050/FLYCAST.pak`

all exist.

- [ ] **Step 3: Run the handheld acceptance smoke checklist**

On both `tg5040` and `tg5050`, verify:

- `GL.pak`
  - Launch a PNG from `Roms/Textures (GL)/...`
  - Expected: animated scene renders instead of a black screen or immediate exit.
  - Open the Minarch menu and return.
  - Expected: menu capture is correct and returning resumes the rendered scene.
  - Trigger screenshot/save state/load state/rewind.
  - Expected: no stale or blank frame after each operation.

- `P64.pak`
  - Launch a `.z64` from `Roms/Nintendo 64 (P64)/...`
  - Expected: gameplay renders through Minarch, not a software fallback.
  - Enable an overlay and a frontend shader.
  - Expected: both still apply on top of gameplay.
  - Save, load, and rewind.
  - Expected: state change redraws correctly and the next frame is fresh.

- `FLYCAST.pak`
  - Launch a `.chd` from `Roms/Sega Dreamcast (FLYCAST)/...`
  - Expected: gameplay renders and the Minarch menu remains usable.
  - Save, load, and rewind.
  - Expected: no stale frame after the operation.
  - Leave an overlay and frontend shader enabled.
  - Expected: both still apply to the final composited output.

- [ ] **Step 4: Run a software-core regression smoke check**

Run:

```bash
bash /Volumes/Storage/GitHub/NextUI_old/workspace/desktop/prepare_fake_sd_root.sh && \
make -C /Volumes/Storage/GitHub/NextUI_old/workspace COMPILE_CORES=1 PLATFORM=desktop all
```

Expected:

- `/var/tmp/nextui/sdcard` exists
- `workspace/all/minarch/build/desktop/minarch.elf` exists
- the desktop core build completes successfully

Then manually launch one existing software core (for example `GB.pak`/`gambatte`) and verify:

- normal gameplay still renders
- the in-game menu still opens
- screenshots/save previews still work

- [ ] **Step 5: Commit the final verified integration**

```bash
git add \
  workspace/all/minarch/minarch.c \
  workspace/all/minarch/hw_video.h \
  workspace/all/minarch/hw_video.c \
  workspace/all/minarch/tests/test_hw_video.c \
  workspace/all/minarch/tests/test_hw_renderer_contract.c \
  workspace/all/minarch/makefile \
  workspace/all/common/api.h \
  workspace/all/common/generic_video.c
git commit -m "feat: add GLES HW render support to Minarch"
```
