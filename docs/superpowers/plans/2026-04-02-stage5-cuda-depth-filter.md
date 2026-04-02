# Stage 5 — CUDA Depth Filter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the CPU Turbo colormap loop with a full GPU pipeline (CUDA → PBO → GL texture) and add two toggleable depth filters: 3×3 median (spatial) and per-pixel EMA (temporal).

**Architecture:** A new `DepthProcessor` class owns all CUDA state (device buffers, PBO, graphics resource). `OakStreams::poll()` delegates colormap + filtering to it. The rest of the codebase only sees a `ui::DepthFilter` enum and sidebar controls.

**Tech Stack:** CUDA 12, OpenGL PBO interop (`cudaGraphicsGLRegisterBuffer`), CMake CUDA language support, ImGui radio buttons + SliderFloat.

---

## File Map

| Action | File | What changes |
|---|---|---|
| Modify | `CMakeLists.txt` | Add CUDA language, CUDAToolkit, DepthProcessor.cu, CUDA arch |
| Create | `src/ui/DepthFilter.h` | `ui::DepthFilter` enum (Off/Spatial/Temporal/Both) |
| Create | `src/cuda/DepthProcessor.h` | Class declaration + CUDA headers |
| Create | `src/cuda/DepthProcessor.cu` | Three kernels + init/destroy/process |
| Modify | `src/oak/OakStreams.h` | Forward-declare DepthProcessor, include DepthFilter.h, update poll() |
| Modify | `src/oak/OakStreams.cpp` | Include DepthProcessor.h, add member, remove CPU loop, update poll() |
| Modify | `src/ui/Sidebar.h` | Add `DepthFilter&` and `float&` params to renderSidebar() |
| Modify | `src/ui/Sidebar.cpp` | Add radio buttons + EMA slider section |
| Modify | `src/main.cpp` | Add depthFilter + emaAlpha locals, pass to sidebar and poll() |

---

## Task 1: CMakeLists.txt — enable CUDA

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add CUDA language and CUDAToolkit**

Replace lines 1-2 of `CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(oak-explorer LANGUAGES CXX)
```
With:
```cmake
cmake_minimum_required(VERSION 3.24)
project(oak-explorer LANGUAGES CXX CUDA)
```
Then add after the `find_package(OpenGL REQUIRED)` line (line 10):
```cmake
find_package(CUDAToolkit REQUIRED)
```

- [ ] **Step 2: Add DepthProcessor.cu to the executable sources**

In the `add_executable` block, after `src/ui/DepthView.cpp`, add:
```cmake
    src/cuda/DepthProcessor.cu
```

- [ ] **Step 3: Add CUDA architecture and link CUDA runtime**

After the `target_include_directories` block, add:
```cmake
set_target_properties(oak-explorer PROPERTIES
    CUDA_ARCHITECTURES "native"   # auto-detects installed GPU; requires CMake 3.24+
)
```

In `target_link_libraries`, add `CUDA::cudart` alongside the existing libs:
```cmake
target_link_libraries(oak-explorer PRIVATE
    depthai::core
    ${GLFW_LIB}
    OpenGL::GL
    CUDA::cudart
)
```

- [ ] **Step 4: Create the cuda source directory and verify CMake configures**

```bash
mkdir src/cuda
```

Then re-run CMake (in VS: Project → Configure, or from terminal):
```bash
cmake --preset windows-debug
```
Expected: configures without error, CUDA compiler detected (nvcc).

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt
git commit -m "build: add CUDA language support and CUDAToolkit"
```

---

## Task 2: DepthFilter enum

**Files:**
- Create: `src/ui/DepthFilter.h`

- [ ] **Step 1: Create the header**

```cpp
// src/ui/DepthFilter.h
#pragma once

namespace ui {

// Controls which GPU depth filters are active each frame.
// Off      — raw depth, colourmap only (GPU path, no filtering)
// Spatial  — 3×3 median filter per frame (kills stereo speckle)
// Temporal — per-pixel EMA across frames (eliminates jitter)
// Both     — median first, then EMA
enum class DepthFilter { Off, Spatial, Temporal, Both };

} // namespace ui
```

- [ ] **Step 2: Commit**
```bash
git add src/ui/DepthFilter.h
git commit -m "feat: add DepthFilter enum (Off/Spatial/Temporal/Both)"
```

---

## Task 3: DepthProcessor — init/destroy + turboLutKernel (Off mode)

**Files:**
- Create: `src/cuda/DepthProcessor.h`
- Create: `src/cuda/DepthProcessor.cu`

- [ ] **Step 1: Create DepthProcessor.h**

```cpp
// src/cuda/DepthProcessor.h
#pragma once
#include <cstdint>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include "ui/DepthFilter.h"

typedef unsigned int GLuint;

namespace cuda {

class DepthProcessor {
public:
    DepthProcessor()  = default;
    ~DepthProcessor() { destroy(); }

    // Allocate device buffers and create/register the PBO.
    // Must be called after the OpenGL context is active.
    void init(int width, int height);

    // Run the filter pipeline and write RGB result into the PBO.
    // hostData: raw uint16 depth values (width*height elements, 0 = invalid).
    // emaAlpha: blend weight for new frame [0.05, 1.0]; ignored when mode
    //           is Off or Spatial.
    void process(const uint16_t* hostData, ui::DepthFilter mode,
                 float emaAlpha = 0.3f);

    // GL PBO id — bind as GL_PIXEL_UNPACK_BUFFER before glTexSubImage2D(nullptr).
    GLuint pboId() const { return pbo_; }

    // Release all CUDA and GL resources. Safe to call multiple times.
    void destroy();

private:
    int width_  = 0;
    int height_ = 0;

    uint16_t* d_input_    = nullptr;  // raw frame, device
    uint16_t* d_filtered_ = nullptr;  // after median, device
    uint16_t* d_ema_      = nullptr;  // persistent EMA state (zeroed at init)

    GLuint                 pbo_     = 0;
    cudaGraphicsResource_t cudaRes_ = nullptr;

    bool initialised_ = false;
};

} // namespace cuda
```

- [ ] **Step 2: Create DepthProcessor.cu — Turbo LUT data + turboLutKernel**

```cpp
// src/cuda/DepthProcessor.cu

// OpenGL must be visible before cuda_gl_interop.h
#include <GLFW/glfw3.h>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>

#include "cuda/DepthProcessor.h"
#include <cstdio>
#include <cstring>

namespace cuda {

// -----------------------------------------------------------------------
// Turbo colormap — 256 entries, uint8 RGB
// Index 0 = far (10000 mm) = dark purple
// Index 255 = close (400 mm) = dark red
// Source: Anton Mikhailov / Google Turbo colormap
// -----------------------------------------------------------------------
static const uint8_t kTurboLutData[256][3] = {
    {48,18,59},{50,21,67},{51,24,74},{52,27,81},{53,30,88},{54,33,95},{55,36,102},{56,39,109},
    {57,42,115},{58,45,121},{59,47,128},{60,50,134},{61,53,139},{62,56,145},{63,59,150},{63,62,156},
    {64,64,161},{65,67,166},{65,70,171},{66,73,176},{67,75,181},{67,78,186},{68,81,191},{68,84,195},
    {69,86,199},{70,89,203},{70,92,207},{71,95,211},{71,97,215},{72,100,218},{72,103,222},{73,106,225},
    {74,108,228},{74,111,231},{75,114,234},{75,116,237},{76,119,240},{76,122,242},{77,124,244},{78,127,246},
    {79,130,248},{79,132,250},{80,135,251},{80,138,252},{81,140,253},{82,143,254},{83,145,254},{84,148,254},
    {85,150,254},{86,153,253},{87,155,252},{88,158,251},{90,160,250},{91,163,248},{92,165,246},{94,168,244},
    {95,170,242},{97,172,240},{99,175,237},{100,177,235},{102,179,232},{104,181,229},{106,184,226},{107,186,223},
    {109,188,220},{111,190,217},{113,192,214},{115,194,211},{117,196,207},{119,198,204},{121,200,201},{123,202,197},
    {125,203,194},{127,205,190},{129,207,187},{131,208,183},{133,210,179},{135,211,176},{137,213,172},{139,214,168},
    {141,216,164},{143,217,161},{145,218,157},{147,220,153},{149,221,149},{151,222,145},{153,223,141},{155,224,137},
    {157,225,133},{160,226,129},{162,227,125},{164,228,121},{166,229,117},{168,230,113},{170,231,109},{172,232,105},
    {174,232,101},{176,233,97},{178,234,93},{180,235,89},{182,235,85},{184,236,81},{186,237,77},{188,237,73},
    {190,238,69},{192,239,65},{194,239,61},{196,240,57},{198,240,53},{200,241,49},{202,241,45},{204,242,41},
    {206,242,37},{208,243,33},{210,243,29},{212,244,25},{214,244,22},{216,244,18},{218,245,14},{220,245,10},
    {222,246,7},{224,246,4},{226,246,2},{228,247,0},{230,247,0},{232,248,0},{234,248,0},{236,248,0},
    {238,249,0},{239,249,0},{241,249,0},{243,249,0},{245,249,0},{246,249,0},{248,249,0},{250,248,0},
    {251,248,0},{253,248,0},{254,247,0},{255,247,0},{255,246,0},{255,245,0},{255,244,0},{255,243,0},
    {255,242,0},{254,241,0},{254,239,0},{254,238,0},{254,237,0},{253,235,0},{253,234,0},{252,232,0},
    {252,231,0},{251,229,0},{251,227,0},{250,226,0},{250,224,0},{249,222,0},{248,220,0},{248,218,0},
    {247,216,0},{246,214,0},{246,212,0},{245,210,0},{244,208,0},{243,206,0},{242,204,0},{241,202,0},
    {241,200,0},{240,198,0},{239,195,0},{238,193,0},{237,191,0},{236,188,0},{235,186,0},{234,183,0},
    {233,181,0},{232,178,0},{231,176,0},{230,173,0},{229,170,0},{228,168,0},{227,165,0},{226,162,0},
    {224,159,0},{223,156,0},{222,153,0},{220,150,0},{219,147,0},{217,144,0},{216,141,0},{214,138,0},
    {213,135,0},{211,132,0},{209,129,0},{208,125,0},{206,122,0},{204,119,0},{202,115,0},{201,112,0},
    {199,108,0},{197,105,0},{195,101,0},{193,97,0},{191,94,0},{189,90,0},{187,86,0},{185,82,0},
    {183,78,0},{181,74,0},{179,70,0},{177,66,0},{174,62,0},{172,58,0},{170,54,0},{167,50,0},
    {165,45,0},{162,41,0},{160,36,0},{157,32,0},{154,27,0},{152,22,0},{149,17,0},{146,12,0},
    {143,7,0},{140,3,0},{137,0,0},{134,0,0},{131,0,0},{128,0,0},{125,0,0},{122,0,0},
    {119,0,0},{116,0,0},{113,0,0},{110,0,0},{107,0,0},{104,0,0},{101,0,0},{98,0,0},
    {95,0,0},{92,0,0},{89,0,0},{86,0,0},{83,0,0},{80,0,0},{77,0,0},{74,0,0},
    {71,0,0},{67,0,0},{63,0,0},{58,0,0},{53,0,0},{47,0,0},{40,0,0},{30,0,0},
};

__constant__ uint8_t kTurboLut[256][3];

// -----------------------------------------------------------------------
// Kernels
// -----------------------------------------------------------------------

// turboLutKernel: maps uint16 depth → RGB via Turbo LUT, writes to PBO.
// d == 0 (invalid) → black.
__global__ void turboLutKernel(const uint16_t* __restrict__ src,
                                uint8_t* __restrict__ dst,
                                int width, int height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int      i = y * width + x;
    uint16_t d = src[i];

    if (d == 0) {
        dst[i*3]   = 0;
        dst[i*3+1] = 0;
        dst[i*3+2] = 0;
        return;
    }

    int idx = (int)((10000.0f - (float)d) / 9600.0f * 255.0f);
    idx = max(0, min(255, idx));
    dst[i*3]   = kTurboLut[idx][0];
    dst[i*3+1] = kTurboLut[idx][1];
    dst[i*3+2] = kTurboLut[idx][2];
}

// -----------------------------------------------------------------------
// DepthProcessor implementation
// -----------------------------------------------------------------------

void DepthProcessor::init(int width, int height) {
    width_  = width;
    height_ = height;
    int n   = width * height;

    cudaMalloc(&d_input_,    n * sizeof(uint16_t));
    cudaMalloc(&d_filtered_, n * sizeof(uint16_t));
    cudaMalloc(&d_ema_,      n * sizeof(uint16_t));
    cudaMemset(d_ema_, 0, n * sizeof(uint16_t));

    // Upload Turbo LUT to __constant__ memory once
    cudaMemcpyToSymbol(kTurboLut, kTurboLutData, sizeof(kTurboLutData));

    // Create PBO — sized width*height*3 bytes (RGB)
    glGenBuffers(1, &pbo_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, n * 3, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    // Register PBO with CUDA — write-discard since we overwrite every frame
    cudaGraphicsGLRegisterBuffer(&cudaRes_, pbo_,
                                 cudaGraphicsRegisterFlagsWriteDiscard);

    initialised_ = true;
    fprintf(stderr, "DepthProcessor: initialised %dx%d\n", width, height);
}

void DepthProcessor::process(const uint16_t* hostData,
                              ui::DepthFilter mode,
                              float emaAlpha)
{
    if (!initialised_) return;

    int n = width_ * height_;
    cudaMemcpy(d_input_, hostData, n * sizeof(uint16_t), cudaMemcpyHostToDevice);

    dim3 block(16, 16);
    dim3 grid((width_ + 15) / 16, (height_ + 15) / 16);

    // Select the buffer to feed into turboLutKernel
    const uint16_t* turboSrc = d_input_;

    // Map PBO — CUDA writes into it, then GL reads from it
    size_t   mappedSize = 0;
    uint8_t* d_pbo      = nullptr;
    cudaGraphicsMapResources(1, &cudaRes_, 0);
    cudaGraphicsResourceGetMappedPointer(
        reinterpret_cast<void**>(&d_pbo), &mappedSize, cudaRes_);

    turboLutKernel<<<grid, block>>>(turboSrc, d_pbo, width_, height_);

    cudaGraphicsUnmapResources(1, &cudaRes_, 0);
}

void DepthProcessor::destroy() {
    if (!initialised_) return;
    cudaGraphicsUnregisterResource(cudaRes_);
    glDeleteBuffers(1, &pbo_);
    cudaFree(d_input_);
    cudaFree(d_filtered_);
    cudaFree(d_ema_);
    d_input_ = d_filtered_ = d_ema_ = nullptr;
    cudaRes_ = nullptr;
    pbo_     = 0;
    initialised_ = false;
}

} // namespace cuda
```

- [ ] **Step 3: Build — verify it compiles (app won't work yet; DepthProcessor is not wired in)**
```bash
cmake --build out/build/debug --config Debug
```
Expected: build succeeds. Zero CUDA errors. `DepthProcessor.cu` compiled by nvcc.

- [ ] **Step 4: Commit**
```bash
git add src/cuda/DepthProcessor.h src/cuda/DepthProcessor.cu
git commit -m "feat: DepthProcessor — init/destroy + turboLutKernel (Off mode)"
```

---

## Task 4: Wire DepthProcessor into OakStreams

**Files:**
- Modify: `src/oak/OakStreams.h`
- Modify: `src/oak/OakStreams.cpp`

This task removes the CPU Turbo colormap loop entirely and replaces it with the GPU path. After this task the depth view should look identical to before (Off mode, GPU colormap).

- [ ] **Step 1: Update OakStreams.h**

Replace the full content of `src/oak/OakStreams.h` with:

```cpp
// src/oak/OakStreams.h
#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include "ui/DepthFilter.h"

typedef unsigned int GLuint;

namespace dai {
    class Device;
    class DataOutputQueue;
    class DataInputQueue;
}
namespace cuda { class DepthProcessor; }

namespace oak {

class OakStreams {
public:
    OakStreams();
    ~OakStreams();

    void open();
    void close();

    // Drain all queues, run GPU depth pipeline, upload textures.
    // filter/emaAlpha default to Off so existing call sites still compile.
    void poll(ui::DepthFilter filter  = ui::DepthFilter::Off,
              float           emaAlpha = 0.3f);

    bool isStreaming() const { return streaming_; }

    GLuint rgbTextureId()   const { return rgbTex_; }
    GLuint depthTextureId() const { return depthTex_; }
    int    rgbWidth()       const { return 1280; }
    int    rgbHeight()      const { return 720; }
    int    depthWidth()     const { return 640; }
    int    depthHeight()    const { return 400; }

    const uint16_t* rawDepthData()    const { return depthBuf_.data(); }
    int             depthPixelCount() const { return depthWidth() * depthHeight(); }

    void setRoi(float normX, float normY);

    bool  hasSpatial()  const { return hasSpatial_; }
    float spatialX()    const { return spatialX_; }
    float spatialY()    const { return spatialY_; }
    float spatialZ()    const { return spatialZ_; }
    float roiCenterX()  const { return roiCenterX_; }
    float roiCenterY()  const { return roiCenterY_; }

    int droppedRgbFrames()   const { return droppedRgb_; }
    int droppedDepthFrames() const { return droppedDepth_; }

private:
    std::unique_ptr<dai::Device>          device_;
    std::shared_ptr<dai::DataOutputQueue> queueRgb_;
    std::shared_ptr<dai::DataOutputQueue> queueDepth_;

    GLuint rgbTex_   = 0;
    GLuint depthTex_ = 0;

    // depthRgbBuf_ removed — GPU writes directly to PBO via DepthProcessor
    std::vector<uint16_t> depthBuf_;     // raw uint16 mm, kept for histogram

    int     droppedRgb_   = 0;
    int     droppedDepth_ = 0;
    int64_t lastRgbSeq_   = -1;
    int64_t lastDepthSeq_ = -1;

    std::shared_ptr<dai::DataInputQueue>  queueSpatialConfig_;
    std::shared_ptr<dai::DataOutputQueue> queueSpatial_;
    float spatialX_   = 0.0f;
    float spatialY_   = 0.0f;
    float spatialZ_   = 0.0f;
    float roiCenterX_ = 0.5f;
    float roiCenterY_ = 0.5f;
    bool  hasSpatial_ = false;

    std::unique_ptr<cuda::DepthProcessor> processor_;

    bool streaming_ = false;
};

} // namespace oak
```

- [ ] **Step 2: Update OakStreams.cpp — add DepthProcessor include and remove CPU colormap**

At the top of `src/oak/OakStreams.cpp`, add the DepthProcessor include after the existing includes:
```cpp
#include "cuda/DepthProcessor.h"
```

Remove the `static constexpr uint8_t kTurboLut[256][3] = { ... };` block entirely (lines 18–53 in the current file). It moves to `DepthProcessor.cu`.

Also remove the `#include <algorithm>` line if it was only used for `std::clamp` in the colormap loop. (Keep it if it's used elsewhere — `setRoi` uses `std::clamp`, so keep it.)

- [ ] **Step 3: Update open() — init DepthProcessor after GL textures**

In `OakStreams::open()`, after the `glPixelStorei(GL_UNPACK_ALIGNMENT, 4);` line that restores alignment (currently near line 180), add:

```cpp
        // Initialise GPU depth processor (must be after GL context + textures)
        processor_ = std::make_unique<cuda::DepthProcessor>();
        processor_->init(640, 400);
```

- [ ] **Step 4: Update close() — destroy DepthProcessor before GL textures**

In `OakStreams::close()`, add before the `if (rgbTex_ != 0)` block:
```cpp
    if (processor_) {
        processor_->destroy();
        processor_.reset();
    }
```

Also remove the `depthRgbBuf_.clear();` line (the vector no longer exists).

- [ ] **Step 5: Update poll() signature and replace CPU colormap with GPU path**

Change the function signature in `OakStreams.cpp` from:
```cpp
void OakStreams::poll() {
```
To:
```cpp
void OakStreams::poll(ui::DepthFilter filter, float emaAlpha) {
```

Inside the depth frame block, replace the entire CPU colormap section:
```cpp
            // Apply Turbo colormap: close (400mm) → idx 255 (red), far (10000mm) → idx 0 (purple)
            uint8_t* rgbBuf = depthRgbBuf_.data();
            for (int i = 0; i < pixelCount; ++i) {
                ... // the loop
            }

            glBindTexture(GL_TEXTURE_2D, depthTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                640, 400,
                GL_RGB, GL_UNSIGNED_BYTE,
                depthRgbBuf_.data());
            glBindTexture(GL_TEXTURE_2D, 0);
```
With the GPU path:
```cpp
            // GPU: filter + Turbo colormap → PBO → texture (zero CPU staging)
            processor_->process(depthBuf_.data(), filter, emaAlpha);

            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, processor_->pboId());
            glBindTexture(GL_TEXTURE_2D, depthTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                640, 400, GL_RGB, GL_UNSIGNED_BYTE,
                nullptr);  // nullptr = read from currently-bound PBO
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
```

- [ ] **Step 6: Build and verify**
```bash
cmake --build out/build/debug --config Debug
```
Expected: zero errors. Launch the app — depth view should look exactly as before (GPU Turbo colormap, filter off).

- [ ] **Step 7: Commit**
```bash
git add src/oak/OakStreams.h src/oak/OakStreams.cpp
git commit -m "feat: wire DepthProcessor into OakStreams — GPU colormap via PBO"
```

---

## Task 5: median3x3Kernel — Spatial mode

**Files:**
- Modify: `src/cuda/DepthProcessor.cu`

- [ ] **Step 1: Add median3x3Kernel after turboLutKernel**

Insert this kernel between the `turboLutKernel` definition and the `DepthProcessor::init` function:

```cuda
// median3x3Kernel: 3×3 median filter on uint16 depth values.
// Invalid pixels (0) → treated as UINT16_MAX so they sort to the high end.
// If all 9 neighbours are invalid, output stays 0.
__global__ void median3x3Kernel(const uint16_t* __restrict__ src,
                                 uint16_t* __restrict__ dst,
                                 int width, int height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    // Gather 3×3 neighbourhood — clamp at edges
    uint16_t v[9];
    int k = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = max(0, min(width  - 1, x + dx));
            int ny = max(0, min(height - 1, y + dy));
            uint16_t val = src[ny * width + nx];
            v[k++] = (val == 0) ? 0xFFFFu : val;  // invalid → sentinel
        }
    }

    // Insertion sort (9 elements — no shared memory needed)
    for (int i = 1; i < 9; i++) {
        uint16_t key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = key;
    }

    // Median is v[4]; convert sentinel back to invalid (0)
    uint16_t median = v[4];
    dst[y * width + x] = (median == 0xFFFFu) ? 0u : median;
}
```

- [ ] **Step 2: Add Spatial mode to process()**

In `DepthProcessor::process()`, replace:
```cpp
    // Select the buffer to feed into turboLutKernel
    const uint16_t* turboSrc = d_input_;
```
With:
```cpp
    const uint16_t* turboSrc = d_input_;

    if (mode == ui::DepthFilter::Spatial || mode == ui::DepthFilter::Both) {
        median3x3Kernel<<<grid, block>>>(d_input_, d_filtered_, width_, height_);
        turboSrc = d_filtered_;
    }
```

- [ ] **Step 3: Build and verify**
```bash
cmake --build out/build/debug --config Debug
```
Launch the app. With filter still wired to Off (we haven't updated the UI yet), depth view looks normal. The kernel compiles and is present — it'll be exercised in Task 7.

- [ ] **Step 4: Commit**
```bash
git add src/cuda/DepthProcessor.cu
git commit -m "feat: median3x3Kernel — spatial depth denoising (Spatial mode)"
```

---

## Task 6: emaKernel — Temporal and Both modes

**Files:**
- Modify: `src/cuda/DepthProcessor.cu`

- [ ] **Step 1: Add emaKernel after median3x3Kernel**

Insert this kernel between `median3x3Kernel` and `DepthProcessor::init`:

```cuda
// emaKernel: per-pixel exponential moving average across frames.
// alpha: weight of the new frame (0.05 = very sticky, 1.0 = no filtering).
// Rules:
//   new == 0          → keep prev (don't let invalid pixels corrupt history)
//   prev == 0         → snap to new (first valid reading, no ramp from 0)
//   otherwise         → alpha*new + (1-alpha)*prev
// Result written back to ema[] (in-place persistent state).
__global__ void emaKernel(const uint16_t* __restrict__ src,
                           uint16_t* __restrict__ ema,
                           int width, int height,
                           float alpha)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int      i      = y * width + x;
    uint16_t newVal = src[i];
    uint16_t prev   = ema[i];

    uint16_t result;
    if (newVal == 0) {
        result = prev;   // invalid — preserve history
    } else if (prev == 0) {
        result = newVal; // first valid reading — snap rather than ramp from 0
    } else {
        result = (uint16_t)(alpha * (float)newVal + (1.0f - alpha) * (float)prev);
    }
    ema[i] = result;
}
```

- [ ] **Step 2: Add Temporal and Both modes to process()**

In `DepthProcessor::process()`, after the `if (mode == Spatial || Both)` block and before the PBO map section, add:

```cpp
    if (mode == ui::DepthFilter::Temporal || mode == ui::DepthFilter::Both) {
        // Temporal input: filtered depth if Both, raw if Temporal only
        const uint16_t* emaSrc = (mode == ui::DepthFilter::Both)
                                 ? d_filtered_ : d_input_;
        emaKernel<<<grid, block>>>(emaSrc, d_ema_, width_, height_, emaAlpha);
        turboSrc = d_ema_;
    }
```

- [ ] **Step 3: Build and verify**
```bash
cmake --build out/build/debug --config Debug
```
Expected: builds cleanly. All three kernels present.

- [ ] **Step 4: Commit**
```bash
git add src/cuda/DepthProcessor.cu
git commit -m "feat: emaKernel — temporal depth stabilisation (Temporal/Both modes)"
```

---

## Task 7: Sidebar UI — DepthFilter radio buttons + EMA slider

**Files:**
- Modify: `src/ui/Sidebar.h`
- Modify: `src/ui/Sidebar.cpp`

- [ ] **Step 1: Update Sidebar.h**

Replace the full content of `src/ui/Sidebar.h` with:

```cpp
// src/ui/Sidebar.h
#pragma once
#include "oak/Device.h"
#include "oak/OakStreams.h"
#include "ui/SpatialMode.h"
#include "ui/DepthFilter.h"

namespace ui {

// Renders the left sidebar. Call inside an active ImGui frame.
// spatialMode and depthFilter are read-write — the sidebar shows the toggles.
// emaAlpha is read-write — the EMA slider modifies it.
void renderSidebar(const oak::OakDevice& device,
                   const oak::OakStreams& streams,
                   SpatialMode&  spatialMode,
                   DepthFilter&  depthFilter,
                   float&        emaAlpha);

} // namespace ui
```

- [ ] **Step 2: Update renderSidebar() signature in Sidebar.cpp**

Change the function signature at the top of `renderSidebar` in `Sidebar.cpp` from:
```cpp
void renderSidebar(const oak::OakDevice& device,
                   const oak::OakStreams& streams,
                   SpatialMode& mode) {
```
To:
```cpp
void renderSidebar(const oak::OakDevice& device,
                   const oak::OakStreams& streams,
                   SpatialMode& spatialMode,
                   DepthFilter& depthFilter,
                   float&       emaAlpha) {
```

Also update all three internal references to `mode` inside the function body — change `mode` → `spatialMode`:
- `bool lockSelected  = (mode == SpatialMode::Lock);` → `(spatialMode == SpatialMode::Lock)`
- `bool trackSelected = (mode == SpatialMode::Track);` → `(spatialMode == SpatialMode::Track)`
- `if (ImGui::RadioButton("Lock",  lockSelected))  mode = SpatialMode::Lock;` → `spatialMode = SpatialMode::Lock`
- `if (ImGui::RadioButton("Track", trackSelected)) mode = SpatialMode::Track;` → `spatialMode = SpatialMode::Track`

- [ ] **Step 3: Add DepthFilter section to Sidebar.cpp**

After the closing `ImGui::Spacing();` of the spatial section (just before the closing `}` of `if (streams.isStreaming())`), add:

```cpp
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextDisabled("Depth filter");

            // Four radio buttons in a 2x2 layout
            bool isOff      = (depthFilter == DepthFilter::Off);
            bool isSpatial  = (depthFilter == DepthFilter::Spatial);
            bool isTemporal = (depthFilter == DepthFilter::Temporal);
            bool isBoth     = (depthFilter == DepthFilter::Both);

            if (ImGui::RadioButton("Off",      isOff))      depthFilter = DepthFilter::Off;
            ImGui::SameLine();
            if (ImGui::RadioButton("Spatial",  isSpatial))  depthFilter = DepthFilter::Spatial;
            if (ImGui::RadioButton("Temporal", isTemporal)) depthFilter = DepthFilter::Temporal;
            ImGui::SameLine();
            if (ImGui::RadioButton("Both",     isBoth))     depthFilter = DepthFilter::Both;

            // EMA strength slider — only visible when temporal filtering is active
            bool temporalActive = (depthFilter == DepthFilter::Temporal ||
                                   depthFilter == DepthFilter::Both);
            ImGui::BeginDisabled(!temporalActive);
            ImGui::Spacing();
            ImGui::TextDisabled("EMA strength");
            ImGui::SetNextItemWidth(-1);  // fill sidebar width
            ImGui::SliderFloat("##ema", &emaAlpha, 0.05f, 1.0f, "%.2f");
            ImGui::EndDisabled();
            ImGui::Spacing();
```

- [ ] **Step 4: Build — will fail because main.cpp call site is stale**
```bash
cmake --build out/build/debug --config Debug
```
Expected: compile error in `main.cpp` — `renderSidebar` called with wrong number of arguments. This is expected; fix it in Task 8.

- [ ] **Step 5: Commit (even with build broken — the sidebar change is self-contained)**
```bash
git add src/ui/Sidebar.h src/ui/Sidebar.cpp
git commit -m "feat: Sidebar — DepthFilter radio buttons + EMA strength slider"
```

---

## Task 8: Wire DepthFilter through main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add includes and local variables**

After `#include "ui/SpatialMode.h"` in `main.cpp`, add:
```cpp
#include "ui/DepthFilter.h"
```

After `ui::SpatialMode spatialMode = ui::SpatialMode::Lock;`, add:
```cpp
    ui::DepthFilter depthFilter = ui::DepthFilter::Off;
    float           emaAlpha    = 0.3f;
```

- [ ] **Step 2: Update renderSidebar call**

Change:
```cpp
        ui::renderSidebar(device, streams, spatialMode);
```
To:
```cpp
        ui::renderSidebar(device, streams, spatialMode, depthFilter, emaAlpha);
```

- [ ] **Step 3: Update streams.poll() call**

Change:
```cpp
        streams.poll();
```
To:
```cpp
        streams.poll(depthFilter, emaAlpha);
```

- [ ] **Step 4: Build and verify**
```bash
cmake --build out/build/debug --config Debug
```
Expected: zero errors.

- [ ] **Step 5: Launch and test all four states**

Launch `oak-explorer`. With a device connected and streaming:

1. **Off** — depth view looks as before (GPU Turbo colormap, raw depth)
2. **Spatial** — scattered speckles should visibly reduce
3. **Temporal** — frame-to-frame jitter stabilises; moving objects leave a slight trail
4. **Both** — cleanest result; speckles gone, jitter gone
5. **EMA slider** — drag toward 1.0 (less filtering) and toward 0.05 (very sticky); visible difference in responsiveness

- [ ] **Step 6: Commit**
```bash
git add src/main.cpp
git commit -m "feat: wire DepthFilter + emaAlpha through main render loop"
```

---

## Post-Implementation

- [ ] **Update README** — mark Stage 5 complete, add Stage 5 section with what it does and key lessons (PBO pattern, `__constant__` memory, persistent GPU state, invalid-pixel EMA handling)
- [ ] **Final commit**
```bash
git add README.md
git commit -m "docs: update README — Stage 5 CUDA depth filter complete"
```
