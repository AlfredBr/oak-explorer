# Stage 5 — CUDA Depth Filtering — Design Spec

**Date:** 2026-04-02
**Status:** Approved
**Project dir:** `E:\luxonis\oak-explorer\`

---

## Overview

Move the depth colormap pipeline from CPU to GPU using CUDA, and add two GPU-accelerated depth filters:

- **Spatial filter** — 3×3 median filter per frame, kills stereo speckle noise
- **Temporal filter** — per-pixel exponential moving average (EMA) across frames, eliminates frame-to-frame jitter

Both filters are independently toggleable from the sidebar, giving four states: Off / Spatial / Temporal / Both. A slider controls the EMA blend factor (α) when temporal filtering is active.

**Milestone:** Depth image is visually stable. Speckles are gone in Spatial mode. Frame-to-frame flicker is gone in Temporal mode. Both active gives the best image quality. The CPU colormap loop is gone entirely.

---

## Decisions Made

| Question | Decision |
|---|---|
| GPU pipeline pattern | PBO interop — CUDA writes into a GL Pixel Buffer Object; `glTexSubImage2D(nullptr)` reads from it |
| CPU colormap loop | Removed entirely — replaced by `turboLutKernel` in CUDA |
| Spatial filter | 3×3 median, invalid pixels (0) treated as UINT16_MAX so they sort out |
| Temporal filter | Per-pixel EMA on uint16 depth values; invalid pixels don't corrupt history |
| EMA α | User-adjustable slider [0.05, 1.0], default 0.3; shown only when temporal active |
| Filter state | `DepthFilter` enum (Off/Spatial/Temporal/Both), same pattern as `SpatialMode` |
| Thread block size | 16×16 threads, grid covers 640×400 |
| Turbo LUT location | Moved from `static constexpr` CPU array to CUDA `__constant__` memory |
| Build system | Add `CUDA` to project languages; `sm_86` default with auto-detect fallback |

---

## Architecture

```
raw uint16 (host, from depthai queue)
    │
    ▼  cudaMemcpy (host → device)
d_input_  (device, uint16, 640×400)
    │
    ├─ [Spatial or Both]
    │   median3x3Kernel(d_input_ → d_filtered_)
    │
    ├─ [Temporal or Both]
    │   emaKernel(d_filtered_ or d_input_, d_ema_, α → d_ema_)
    │   (d_ema_ persists between frames)
    │
    ▼  (result is d_ema_, d_filtered_, or d_input_ depending on mode)
turboLutKernel(uint16 → RGB, written into mapped PBO)
    │
    ▼  glTexSubImage2D(... nullptr)   ← GL reads from PBO, not CPU
depthTex_  (GL texture, unchanged)
```

**Input selection per mode:**

| Mode | median input | EMA input | turboLut input |
|---|---|---|---|
| Off | — | — | d_input_ |
| Spatial | d_input_ | — | d_filtered_ |
| Temporal | — | d_input_ | d_ema_ |
| Both | d_input_ | d_filtered_ | d_ema_ |

---

## New Files

| File | Purpose |
|---|---|
| `src/cuda/DepthProcessor.h` | `DepthFilter` enum + `DepthProcessor` class declaration |
| `src/cuda/DepthProcessor.cu` | Three kernels + class implementation |

---

## `DepthFilter` Enum

Lives in `src/cuda/DepthProcessor.h` (not a separate header — it's small and owned by this subsystem):

```cpp
namespace cuda {
    enum class DepthFilter { Off, Spatial, Temporal, Both };
}
```

---

## `DepthProcessor` Class

```cpp
namespace cuda {

class DepthProcessor {
public:
    DepthProcessor() = default;
    ~DepthProcessor();  // calls destroy()

    // Allocate CUDA buffers and create/register PBO.
    // Must be called after the OpenGL context is active.
    void init(int width, int height);

    // Run the filter pipeline and write result into the PBO.
    // hostData: raw uint16 depth values (width*height elements).
    // mode: which filters to apply.
    // emaAlpha: blend factor for EMA [0.05, 1.0]; ignored when mode is Off or Spatial.
    void process(const uint16_t* hostData, DepthFilter mode, float emaAlpha = 0.3f);

    // GL PBO id — bind as GL_PIXEL_UNPACK_BUFFER before glTexSubImage2D.
    GLuint pboId() const { return pbo_; }

    // Release all CUDA and GL resources.
    void destroy();

private:
    int width_  = 0;
    int height_ = 0;

    uint16_t* d_input_    = nullptr;  // raw frame, device
    uint16_t* d_filtered_ = nullptr;  // after median, device
    uint16_t* d_ema_      = nullptr;  // persistent EMA state, device (zeroed at init)

    GLuint                      pbo_      = 0;
    cudaGraphicsResource_t      cudaRes_  = nullptr;

    bool initialised_ = false;
};

} // namespace cuda
```

---

## Kernels

All three kernels are launched with a `dim3 block(16, 16)` and a grid that covers `width × height`.

### `median3x3Kernel`

```
Input:  d_input_ (uint16)
Output: d_filtered_ (uint16)
```

- Each thread loads its 3×3 neighbourhood into 9 registers.
- Edge pixels clamp coordinates to `[0, width-1] × [0, height-1]`.
- Invalid pixels (value 0) are replaced with `UINT16_MAX` before sorting so they sort to the high end.
- Insertion sort (9 elements — no shared memory needed at this size).
- Output: sorted[4] (the median).
- If all 9 neighbours were invalid (all `UINT16_MAX` after substitution), write 0.

### `emaKernel`

```
Input:  src (uint16, either d_input_ or d_filtered_)
In/Out: d_ema_ (uint16, persistent)
Output: d_ema_ updated in place
```

Per-pixel logic:
```
if (new == 0)
    // invalid pixel — keep history, don't corrupt EMA with a hole
    ema = prev
else if (prev == 0)
    // first valid reading for this pixel — snap rather than ramp from 0
    ema = new
else
    ema = (uint16_t)(α * new + (1 − α) * prev)
```

The EMA buffer starts zeroed at `init()`. The "first valid reading" branch prevents a slow ramp-up from 0 when a pixel comes online.

### `turboLutKernel`

```
Input:  src (uint16)
Output: mapped PBO pointer (uint8, RGB, 3 bytes per pixel)
```

- Turbo LUT stored in `__constant__ uint8_t kTurboLut[256][3]` — transferred once at `init()` via `cudaMemcpyToSymbol`.
- LUT index: `clamp((int)((10000.0f − d) / 9600.0f * 255.0f), 0, 255)`
- Invalid pixel (d == 0): write `{0, 0, 0}` (black).
- Output pointer obtained by mapping `cudaRes_` via `cudaGraphicsMapResources` + `cudaGraphicsResourceGetMappedPointer` before the kernel launch; unmapped immediately after.

---

## Integration with `OakStreams`

### Removed from `OakStreams`

- `depthRgbBuf_` (`std::vector<uint8_t>`) — staging buffer for CPU colormap
- The Turbo LUT `static constexpr` array
- The CPU colormap loop in `poll()`

### Added to `OakStreams`

- `cuda::DepthProcessor processor_` member
- `processor_.init(640, 400)` in `open()`, after GL textures are created
- `processor_.destroy()` in `close()`, before GL texture deletion
- `poll()` gains a `DepthFilter` and `float emaAlpha` parameter:

```cpp
void OakStreams::poll(cuda::DepthFilter filter, float emaAlpha) {
    // ... existing RGB and depth queue drain unchanged ...

    if (depthFrame) {
        // ... sequence number tracking unchanged ...

        // Copy raw depth to depthBuf_ (histogram still reads from here)
        std::copy(depthData, depthData + pixelCount, depthBuf_.data());

        // GPU: filter + colormap → PBO
        processor_.process(depthBuf_.data(), filter, emaAlpha);

        // PBO → GL texture (zero-copy on GPU side)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, processor_.pboId());
        glBindTexture(GL_TEXTURE_2D, depthTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        640, 400, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
}
```

`depthBuf_` (raw uint16) is kept — `DepthView`'s histogram still reads from it.

---

## Render Loop (`main.cpp`)

```cpp
cuda::DepthFilter depthFilter = cuda::DepthFilter::Off;
float             emaAlpha    = 0.3f;

// In the frame loop:
ui::renderSidebar(device, streams, spatialMode, depthFilter, emaAlpha);
ui::renderStreamView(streams, spatialMode);
ui::renderDepthView(streams);
streams.poll(depthFilter, emaAlpha);
```

---

## Sidebar Additions

Below the spatial XYZ section:

```
─────────────────────
Depth filter
(•) Off      ( ) Spatial
( ) Temporal ( ) Both

EMA strength  [====|----]  0.30
  (slider only shown when Temporal or Both)
─────────────────────
```

- Four radio buttons in a 2×2 grid using `ImGui::RadioButton`.
- `emaAlpha` slider: `ImGui::SliderFloat`, range [0.05, 1.0], format `"%.2f"`.
- Slider hidden with `ImGui::BeginDisabled` / `EndDisabled` when mode is Off or Spatial.

---

## Build System Changes (`CMakeLists.txt`)

1. Change `project(oak-explorer LANGUAGES CXX)` → `project(oak-explorer LANGUAGES CXX CUDA)`
2. Add `find_package(CUDAToolkit REQUIRED)`
3. Add `src/cuda/DepthProcessor.cu` to the source list
4. Set CUDA architecture: `set_target_properties(oak-explorer PROPERTIES CUDA_ARCHITECTURES "86")` with a comment noting this targets RTX 30-series; change to `native` or `all-major` for broader compatibility
5. Link `CUDA::cudart` to the target

---

## Data Flow Summary

```
depthai queue
    └─ uint16 depth frame
           ├─ depthBuf_  (kept for histogram)
           └─ DepthProcessor::process()
                  ├─ cudaMemcpy → d_input_
                  ├─ [median3x3Kernel] → d_filtered_
                  ├─ [emaKernel]       → d_ema_  (persistent)
                  └─ turboLutKernel    → PBO
                                            └─ glTexSubImage2D(nullptr) → depthTex_
```

---

## What You'll Learn

- **PBO interop**: `cudaGraphicsGLRegisterBuffer` + map/unmap pattern for zero-copy CUDA→GL transfer
- **`__constant__` memory**: LUT in constant cache — all 256 threads in a warp hit the same address, so it broadcasts rather than serialising
- **Persistent GPU state**: `d_ema_` allocated once, mutated every frame — no per-frame allocation
- **Kernel composition**: chaining kernels with intermediate device buffers, selecting input by pointer based on mode
- **Invalid-pixel handling**: sentinel values (`UINT16_MAX`) and conditional EMA branches — depth data is full of holes

---

## Out of Scope

- Applying filters to the RGB stream
- 5×5 or larger median (3×3 is sufficient for this sensor's noise pattern)
- Temporal median (ring buffer over N frames) — EMA achieves the same visual result with less memory
- CUDA streams / async overlap with CPU work (single stream is fine at 30fps)
- Benchmarking / profiling (no Nsight integration planned)
