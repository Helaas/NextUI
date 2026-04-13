# Minarch GLES HW Render Integration Design

## Summary

Add OpenGL ES libretro hardware-render support to Minarch for `tg5040` and `tg5050` by integrating hardware-rendered cores into the existing frontend-owned GL compositor.

The target is full Minarch parity from day one for the three current pak repos:

- `nextui-libretro-gl-core-pak`
- `nextui-p64-pak`
- `nextui-flycast-pak`

That parity includes:

- in-game menu behavior
- screenshots and save previews
- save/load states
- rewind
- overlays
- the existing frontend shader/compositor path

The shortest acceptable implementation path is to keep one presentation pipeline. Hardware-rendered cores should render into a frontend-managed GLES target, and the existing compositor should remain the only path that produces the final on-screen frame.

## Scope

In scope:

- Minarch and closely related frontend/compositor files only
- GLES hardware-render integration for libretro cores
- `tg5040` and `tg5050`
- preserving current software-core behavior
- final-frame screenshots and previews for hardware-rendered cores

Out of scope:

- Vulkan support
- my355 support
- mouse input support
- creating a second presentation path for hardware-rendered cores
- fallback to per-frame GPU readback as the normal render path

## Assumptions

- The main compatibility gap is inside Minarch, not in the pak launch scripts.
- `parallel-n64` and `flycast` require a real GLES-backed libretro frontend path.
- `libretro-gl` also expects mouse input, but mouse support is not required for this work and can remain unsupported.
- User-visible parity matters more than preserving a distinction between raw core output and final composited output.
- Final-frame capture is acceptable for screenshots and save previews if that is the shortest path to implementation.

## Constraints

- Vulkan is explicitly unsupported.
- The existing Minarch shader/compositor pipeline must continue to own final presentation.
- Existing software-rendered cores must keep working without behavioral regressions.
- Any new code should be narrowly scoped to hardware-render integration and should not refactor unrelated rendering code.

## Approaches Considered

### 1. Extend the existing GL compositor to host libretro HW rendering

Recommended.

Minarch already owns an SDL GL context and an offscreen FBO/texture-based compositor. This design makes hardware-rendered libretro cores render into a frontend-managed GLES target, then routes that target through the same compositor path already used for final presentation, screenshots, overlays, notifications, and shader passes.

Pros:

- shortest path to full Minarch parity
- one presentation stack
- final-frame screenshots and previews work naturally
- existing overlays and shader passes stay shared

Cons:

- requires careful libretro HW callback integration
- requires a small compositor extension so its first input can be a GL texture instead of CPU pixels

### 2. Read back GPU frames into CPU memory and keep the current software pipeline

Rejected.

This would preserve the current software-oriented Minarch frame path, but it would turn hardware-rendered cores into readback-heavy software uploads every frame.

Pros:

- smaller conceptual change in the libretro frame callback path

Cons:

- likely too slow for `parallel-n64` and `flycast`
- preserves feature parity by undermining performance
- still adds special-case logic without solving the real integration problem cleanly

### 3. Build a separate hardware-render presentation path

Rejected.

This would make hardware-rendered cores bypass the existing compositor and then reintroduce missing parity features one by one.

Pros:

- isolates the software path

Cons:

- duplicates presentation logic
- increases maintenance cost
- highest risk of divergence in screenshots, overlays, menu behavior, and shaders

## Chosen Design

Minarch remains the owner of a single SDL GLES context. When a libretro core requests hardware rendering through `RETRO_ENVIRONMENT_SET_HW_RENDER`, Minarch accepts only GLES contexts, creates a frontend-owned render target for that core, and exposes it through the standard libretro callbacks.

The core then renders directly into the frontend-managed target. When it submits `RETRO_HW_FRAME_BUFFER_VALID`, Minarch records the frame metadata and hands the compositor the hardware-rendered texture as its source image. The compositor then applies the same existing frontend processing used for software cores:

- scaling and aspect handling
- frontend shader chain
- overlays
- notification layer
- final swap
- GL screenshot capture

This preserves one final presentation path for both software and hardware-rendered cores.

## Architecture

### Frontend-owned GLES target

For hardware-rendered cores, Minarch creates and owns:

- a color texture
- an FBO
- a depth/stencil attachment when requested by the core

The core does not own presentation. It only renders into the frontend-owned target via the libretro hardware-render callback contract.

### Minarch-owned HW session state

Minarch stores a compact hardware-render session state that tracks:

- whether the loaded core uses hardware rendering
- requested GLES context type and version
- copied `retro_hw_render_callback`
- frontend-owned GL object ids
- current hardware frame width and height
- current hardware frame validity
- current aspect metadata

This state is the switch between the existing software frame path and the new hardware-render path.

### Unified compositor

The current compositor remains the only code that presents frames to the display.

Its input selection becomes:

- software source buffer for software-rendered cores
- GL texture source for hardware-rendered cores

After that first input selection, the rest of the compositor path stays shared.

## Component Responsibilities

### `workspace/all/minarch/minarch.c`

Responsibilities:

- accept and validate `RETRO_ENVIRONMENT_SET_HW_RENDER`
- reject non-GLES contexts
- store the hardware-render callback
- populate `get_current_framebuffer` and `get_proc_address`
- manage per-core hardware-render session state
- handle hardware-render frame submission in `video_refresh_callback`
- invalidate and refresh frame validity after state load, rewind, resume, or reset
- call core `context_reset` and `context_destroy` at the right times

### `workspace/all/common/generic_video.c`

Responsibilities:

- create, resize, and destroy the frontend-owned hardware-render target
- expose the current FBO id and GL proc lookup
- let the compositor accept a GL texture source
- continue to own final presentation, overlays, notifications, shader passes, and screenshot capture

### `workspace/all/common/api.h`

Responsibilities:

- expose only the minimal additional frontend API needed for Minarch-to-compositor integration

No new generic rendering abstraction is required beyond the immediate needs of this feature.

## Libretro Environment Behavior

### Supported

- `RETRO_ENVIRONMENT_SET_HW_RENDER`
- `RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO`
- `RETRO_ENVIRONMENT_SET_GEOMETRY`
- existing save-state, rumble, options, and controller behavior already used by Minarch

### Explicitly unsupported for this work

- Vulkan hardware rendering
- non-GLES hardware-render context types
- mouse input

### Current policy for unsupported requests

If a core requests unsupported hardware rendering, Minarch should fail clearly during load rather than silently attempting a degraded path.

## Runtime Data Flow

### Core startup

1. Minarch loads the core as it does today.
2. If the core requests hardware rendering:
   - Minarch verifies that the request is GLES-only.
   - Minarch stores the callback and fills in frontend-owned function pointers.
   - Minarch creates or resizes the frontend render target.
   - Minarch invokes the core `context_reset`.
3. If the core does not request hardware rendering, the current software path remains unchanged.

### Per-frame rendering

Software core path:

1. Core submits CPU-backed frame data.
2. Minarch converts it as needed.
3. The existing compositor uploads and presents it.

Hardware-render path:

1. Core binds the frontend-owned FBO from `get_current_framebuffer`.
2. Core renders directly into the frontend-owned target.
3. Core calls `video_refresh(RETRO_HW_FRAME_BUFFER_VALID, width, height, 0)`.
4. Minarch records frame geometry and marks the hardware frame as valid.
5. The compositor samples the core texture directly as its input.
6. Existing frontend shaders, overlays, notifications, and final swap run unchanged after that point.

### Dupe frames

Dupe-frame behavior should continue to work for both paths. Hardware-rendered cores should not force readback or CPU conversion for frame duplication.

## Geometry And Resizing

- `SET_SYSTEM_AV_INFO` and `SET_GEOMETRY` continue to update FPS, sample rate, and aspect data.
- For hardware-rendered cores, width or height changes also trigger render-target resize before presentation of the next frame.
- The compositor continues to own device-fit logic for scaling and aspect ratio.

## Menu, Screenshots, Save Previews, And Overlays

To keep parity with the shortest implementation path:

- screenshots use the current GL screenshot capture path from the final composited framebuffer
- menu background capture uses the same final composited framebuffer
- save preview images come from the same final composited framebuffer

This means hardware-rendered cores get the same visible output semantics as software cores from the player's perspective, without adding a second capture system.

Overlays, notifications, ambient effects, and frontend shader passes remain post-core compositing operations and apply equally to software and hardware-rendered cores.

## Save States And Rewind

Save-state serialization remains core-owned. Minarch's save/load/rewind logic does not split into separate CPU and GPU implementations.

After:

- load state
- rewind step
- resume/autoload
- controlled GL context reset

Minarch should invalidate the current hardware-frame-ready latch so the next visible frame comes from a fresh core render rather than stale GPU content.

This keeps save/load and rewind behavior aligned with the existing Minarch model while avoiding extra GPU readback logic.

## Context Lifecycle

### On controlled startup

- create or resize frontend render target
- call core `context_reset`

### On controlled shutdown

- call core `context_destroy` if present
- destroy frontend-owned GL objects

### On frontend-side recreation or loss

- recreate frontend-owned GL objects
- call core `context_reset`

No separate recovery path based on copying GPU output into software buffers is planned.

## Failure Handling

- Reject unsupported non-GLES hardware-render requests clearly.
- If render-target allocation fails, fail core load instead of falling back to a broken or slow path.
- If a core has not yet produced a valid hardware frame, preserve existing dupe-frame semantics instead of presenting undefined GPU contents.
- If a controlled context reset occurs, rebuild frontend-owned targets and reissue the core reset callback.

## Verification Criteria

Implementation is successful when all of the following are true on `tg5040` and `tg5050`:

- `GL.pak` boots and displays correctly through Minarch
- `P64.pak` boots and displays correctly through Minarch
- `FLYCAST.pak` boots and displays correctly through Minarch
- hardware-rendered cores continue to use:
  - in-game menu
  - screenshots and save previews
  - save/load states
  - rewind
  - overlays
  - existing frontend shader/compositor path
- at least one representative software-rendered core still works correctly after the change

## Verification Strategy

### `GL.pak`

Use it to validate:

- basic GLES hardware-render callback wiring
- final-frame screenshot behavior
- menu background capture behavior

### `P64.pak`

Use it to validate:

- hardware rendering through the compositor
- save/load states
- rewind
- return to and from the in-game menu

### `FLYCAST.pak`

Use it to validate:

- hardware rendering through the compositor under a more demanding core
- overlay and shader interactions
- save/load and rewind behavior under the hardware-render path

### Software-core regression check

Smoke-test at least one existing software-rendered core afterward to catch regressions in the current software path.

## Risks

- libretro HW-render lifecycle ordering may expose assumptions in Minarch's current startup and shutdown flow.
- The existing compositor is byte-buffer-oriented, so its source selection must be extended carefully without destabilizing software-core rendering.
- State load and rewind may reveal stale-frame bugs if hardware-frame validity is not invalidated consistently.

## Non-goals

This design does not attempt to:

- support Vulkan
- support mouse-driven gameplay
- redesign the compositor
- refactor unrelated rendering code
- create a generic multi-backend renderer abstraction

## Implementation Direction

The implementation should be surgical:

- add minimal hardware-render session state in Minarch
- add minimal frontend GL target helpers
- extend the compositor only where needed so its first source can be a GL texture
- keep all existing post-source compositor logic shared

Every changed line should trace back to enabling GLES hardware-rendered cores inside the existing Minarch feature set.
