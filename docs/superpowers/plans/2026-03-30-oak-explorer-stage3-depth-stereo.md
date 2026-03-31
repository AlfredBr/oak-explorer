# Stage 3 — Depth & Stereo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a live depth heatmap alongside the RGB stream — StereoDepth pipeline, Turbo colormap applied CPU-side, depth texture uploaded to OpenGL, rendered in a separate ImGui window with an ImPlot histogram below it.

**Architecture:** Replace `CameraStream` with `OakStreams` — a single class that owns the one `dai::Device`, builds a combined pipeline (RGB + stereo depth), and exposes two GL textures plus the raw depth buffer for histogram rendering. Two new UI panels: `DepthView` (heatmap + histogram) alongside the existing `StreamView` (RGB).

**Tech Stack:** depthai-core 2.32.0, OpenGL 3.3, ImGui docking branch, ImPlot v0.16 (vendored), GLFW 3.4, MSVC C++17, VS2026 Insiders

---

## Context for implementers

- Working directory: `E:\luxonis\oak-explorer\`
- Build command (use terminal — VS2026 Insiders has a bug where MSBuild re-runs lose cacheVariables):
  ```bash
  cmake --preset windows-debug
  cmake --build out/build/windows-debug --config Debug
  ```
- Run: `out\build\windows-debug\Debug\oak-explorer.exe`
- Stage 2 is complete — RGB camera stream works, `CameraStream` is the current stream class
- **No unit test framework.** Verification = build succeeds (0 errors) + visual/runtime confirmation
- Device must be plugged in via USB3 for runtime testing
- `setInterleaved(true)` is required for GL_RGB — learned in Stage 2; use it for the RGB camera node
- Use `#include <GLFW/glfw3.h>` for OpenGL, not `<GL/GL.h>` — Windows SDK GL.h causes WINGDIAPI/APIENTRY conflicts with depthai headers

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `third_party/implot/` | Create (download) | ImPlot v0.16 vendored sources |
| `CMakeLists.txt` | Modify | Add implot sources + include dir; swap CameraStream.cpp → OakStreams.cpp; add DepthView.cpp |
| `src/oak/OakStreams.h` | Create | Interface: open/close/poll, two textures, raw depth buffer |
| `src/oak/OakStreams.cpp` | Create | Pipeline (RGB + StereoDepth), Turbo LUT, colormap, two GL textures |
| `src/ui/DepthView.h` | Create | Interface: renderDepthView(const OakStreams&) |
| `src/ui/DepthView.cpp` | Create | ImGui "Depth" window: heatmap image + ImPlot histogram |
| `src/ui/Sidebar.h` | Modify | CameraStream& → OakStreams& |
| `src/ui/Sidebar.cpp` | Modify | CameraStream& → OakStreams& |
| `src/ui/StreamView.h` | Modify | CameraStream& → OakStreams& |
| `src/ui/StreamView.cpp` | Modify | CameraStream& → OakStreams&; accessor names: textureId→rgbTextureId, width→rgbWidth, height→rgbHeight |
| `src/main.cpp` | Modify | Swap CameraStream→OakStreams; add ImPlot init/destroy; add renderDepthView call |
| `src/oak/CameraStream.h` | Delete | Replaced by OakStreams |
| `src/oak/CameraStream.cpp` | Delete | Replaced by OakStreams |

---

## Task 1: Vendor ImPlot v0.16

**Files:**
- Create: `third_party/implot/` (directory + 4 files)

- [ ] **Step 1.1: Download ImPlot v0.16**

In a terminal at `E:\luxonis\oak-explorer\`:

```bash
cd third_party
curl -L "https://github.com/epezent/implot/archive/refs/tags/v0.16.zip" -o implot-v0.16.zip
```

If curl fails, download manually from GitHub: `https://github.com/epezent/implot/releases/tag/v0.16`

- [ ] **Step 1.2: Extract the 4 required files**

From the zip, extract only these files into `third_party/implot/`:
- `implot.h`
- `implot_internal.h`
- `implot.cpp`
- `implot_items.cpp`

The zip contains `implot-0.16/` as a subdirectory — copy the files out, not the whole folder. Result:

```
third_party/
├── imgui/          ← unchanged
└── implot/
    ├── implot.h
    ├── implot_internal.h
    ├── implot.cpp
    └── implot_items.cpp
```

- [ ] **Step 1.3: Verify files are in place**

```bash
ls third_party/implot/
```

Expected: 4 files listed above. If there are extra files (like `backends/`, `docs/`), that's fine but not needed.

- [ ] **Step 1.4: Commit**

```bash
cd E:\luxonis\oak-explorer
git add third_party/implot/
git commit -m "vendor: add ImPlot v0.16 to third_party"
```

---

## Task 2: Update CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 2.1: Update the CMakeLists.txt**

Replace the entire file with:

```cmake
cmake_minimum_required(VERSION 3.20)
project(oak-explorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Dependencies ---
find_package(depthai CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# GLFW: prebuilt zip does not ship a CMake config — use find_library directly
find_library(GLFW_LIB glfw3 PATHS "${GLFW_ROOT}/lib-vc2022" REQUIRED)
set(GLFW_INCLUDE_DIR "${GLFW_ROOT}/include")

# --- ImGui sources (vendored) ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# --- ImPlot sources (vendored) ---
set(IMPLOT_DIR ${CMAKE_SOURCE_DIR}/third_party/implot)

# --- Executable ---
add_executable(oak-explorer
    src/main.cpp
    src/oak/Device.cpp
    src/oak/OakStreams.cpp
    src/ui/Sidebar.cpp
    src/ui/StreamView.cpp
    src/ui/DepthView.cpp
    ${IMGUI_SOURCES}
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)

target_include_directories(oak-explorer PRIVATE
    src/
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLFW_INCLUDE_DIR}
    ${IMPLOT_DIR}
)

target_link_libraries(oak-explorer PRIVATE
    depthai::core
    ${GLFW_LIB}
    OpenGL::GL
)

# Copy depthai DLLs next to the executable on Windows
# DEPTHAI_WIN64_DIR is set in CMakePresets.json cacheVariables
if(WIN32)
    add_custom_command(TARGET oak-explorer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DEPTHAI_WIN64_DIR}/bin"
            $<TARGET_FILE_DIR:oak-explorer>
        COMMENT "Copying depthai DLLs from ${DEPTHAI_WIN64_DIR}/bin"
    )
endif()
```

Changes from Stage 2:
- `CameraStream.cpp` removed from sources
- `OakStreams.cpp` added
- `DepthView.cpp` added
- `IMPLOT_DIR` variable added
- `implot.cpp` and `implot_items.cpp` added to sources
- `${IMPLOT_DIR}` added to include directories

- [ ] **Step 2.2: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add OakStreams, DepthView, ImPlot to CMakeLists"
```

---

## Task 3: Write OakStreams.h

**Files:**
- Create: `src/oak/OakStreams.h`

- [ ] **Step 3.1: Create the header**

Create `src/oak/OakStreams.h` with this exact content:

```cpp
// src/oak/OakStreams.h
#pragma once
#include <memory>
#include <vector>
#include <cstdint>

// Forward-declare GLuint to avoid pulling OpenGL headers into every TU
typedef unsigned int GLuint;

// Forward-declare depthai types used in private members
namespace dai {
    class Device;
    class DataOutputQueue;
}

namespace oak {

// OakStreams owns the one dai::Device, builds a combined RGB + StereoDepth
// pipeline, and exposes two GL textures plus the raw depth buffer.
// All depthai and OpenGL concerns live here; the UI layer never touches either.
class OakStreams {
public:
    OakStreams();
    ~OakStreams();  // calls close()

    // Build pipeline, open device, create both GL textures.
    // Must be called after the OpenGL context is active.
    // Does not throw. Logs to stderr on failure; isStreaming() stays false.
    void open();

    // Release device and both GL textures.
    // Safe to call at any time, including if open() never succeeded.
    void close();

    // Drain both queues, apply Turbo colormap to depth, upload both textures.
    // Never throws. Call once per render frame before ImGui::NewFrame().
    void poll();

    bool isStreaming() const { return streaming_; }

    // GL textures for rendering
    GLuint rgbTextureId()   const { return rgbTex_; }
    GLuint depthTextureId() const { return depthTex_; }
    int    rgbWidth()       const { return 1280; }
    int    rgbHeight()      const { return 720; }
    int    depthWidth()     const { return 640; }
    int    depthHeight()    const { return 400; }

    // Raw uint16 depth values in mm, 0 = invalid. Updated each poll().
    // Used by DepthView to build the ImPlot histogram.
    const uint16_t* rawDepthData()    const { return depthBuf_.data(); }
    int             depthPixelCount() const { return depthWidth() * depthHeight(); }

private:
    std::unique_ptr<dai::Device>          device_;
    std::shared_ptr<dai::DataOutputQueue> queueRgb_;
    std::shared_ptr<dai::DataOutputQueue> queueDepth_;

    GLuint rgbTex_   = 0;
    GLuint depthTex_ = 0;

    std::vector<uint8_t>  depthRgbBuf_;  // 640*400*3 — Turbo-colored staging buffer
    std::vector<uint16_t> depthBuf_;     // 640*400   — raw depth values for histogram

    bool streaming_ = false;
};

} // namespace oak
```

- [ ] **Step 3.2: Commit**

```bash
git add src/oak/OakStreams.h
git commit -m "feat: add OakStreams header"
```

---

## Task 4: Write OakStreams.cpp

**Files:**
- Create: `src/oak/OakStreams.cpp`

- [ ] **Step 4.1: Create the implementation**

Create `src/oak/OakStreams.cpp` with this content:

```cpp
// src/oak/OakStreams.cpp
#include "oak/OakStreams.h"

#include <depthai/depthai.hpp>
#include <GLFW/glfw3.h>   // Use GLFW's header, not <GL/GL.h> — avoids WINGDIAPI conflict with depthai

#include <algorithm>  // std::clamp
#include <cstdio>

namespace oak {

// Turbo colormap LUT — 256 entries, uint8 RGB
// Index 0 = far (10000mm) = dark purple
// Index 255 = close (400mm) = dark red
// Source: canonical Turbo colormap by Anton Mikhailov (Google)
// Verify against https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f
// if colormap output looks wrong (first 128 entries below are canonical).
static constexpr uint8_t kTurboLut[256][3] = {
    // idx 0-127: dark purple → blue → cyan → green → yellow
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
    // idx 128-255: yellow → orange → dark red
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

OakStreams::OakStreams()  = default;
OakStreams::~OakStreams() { close(); }

void OakStreams::open() {
    try {
        // --- Build pipeline ---
        dai::Pipeline pipeline;

        // RGB camera — 1280×720, interleaved RGB (required for GL_RGB upload)
        auto cam = pipeline.create<dai::node::ColorCamera>();
        cam->setPreviewSize(1280, 720);
        cam->setInterleaved(true);   // REQUIRED for GL_RGB — false outputs planar CHW
        cam->setColorOrder(dai::ColorCameraProperties::ColorOrder::RGB);
        cam->setFps(30);

        auto xoutRgb = pipeline.create<dai::node::XLinkOut>();
        xoutRgb->setStreamName("rgb");
        cam->preview.link(xoutRgb->input);

        // Stereo cameras — 640×400 each
        auto monoLeft  = pipeline.create<dai::node::MonoCamera>();
        auto monoRight = pipeline.create<dai::node::MonoCamera>();

        monoLeft->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        monoLeft->setBoardSocket(dai::CameraBoardSocket::CAM_B);  // left on OAK-D-Lite

        monoRight->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        monoRight->setBoardSocket(dai::CameraBoardSocket::CAM_C);  // right on OAK-D-Lite

        // StereoDepth — HIGH_DENSITY preset, depth output in mm as uint16
        auto stereo = pipeline.create<dai::node::StereoDepth>();
        stereo->setDefaultProfilePreset(dai::node::StereoDepth::PresetType::HIGH_DENSITY);

        monoLeft->out.link(stereo->left);
        monoRight->out.link(stereo->right);

        auto xoutDepth = pipeline.create<dai::node::XLinkOut>();
        xoutDepth->setStreamName("depth");
        stereo->depth.link(xoutDepth->input);   // depth output, NOT disparity — uint16 mm

        // --- Open device (boots pipeline onto VPU) ---
        device_ = std::make_unique<dai::Device>(pipeline);

        // --- Get output queues (non-blocking, 4-frame buffer) ---
        queueRgb_   = device_->getOutputQueue("rgb",   4, false);
        queueDepth_ = device_->getOutputQueue("depth", 4, false);

        // --- Allocate staging buffers ---
        depthRgbBuf_.resize(640 * 400 * 3);  // Turbo-colored RGB for GL upload
        depthBuf_.resize(640 * 400);          // raw uint16 mm values for histogram

        // --- Create RGB GL texture (1280×720) ---
        // 1280×3 = 3840 bytes/row — set alignment=1 to be safe (matches Stage 2 working pattern)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glGenTextures(1, &rgbTex_);
        glBindTexture(GL_TEXTURE_2D, rgbTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
            1280, 720, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        // --- Create depth GL texture (640×400) ---
        // 640×3 = 1920 bytes/row — 4-byte aligned, no alignment change needed
        glGenTextures(1, &depthTex_);
        glBindTexture(GL_TEXTURE_2D, depthTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
            640, 400, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // restore default

        streaming_ = true;
        fprintf(stderr, "OakStreams: opened — RGB 1280x720, Depth 640x400 @ 30fps\n");

    } catch (const std::exception& e) {
        fprintf(stderr, "OakStreams::open failed: %s\n", e.what());
        close();
    } catch (...) {
        fprintf(stderr, "OakStreams::open failed: unknown exception\n");
        close();
    }
}

void OakStreams::close() {
    streaming_ = false;
    queueRgb_.reset();    // release queues before device
    queueDepth_.reset();
    device_.reset();      // destructor stops pipeline
    if (rgbTex_ != 0) {
        glDeleteTextures(1, &rgbTex_);
        rgbTex_ = 0;
    }
    if (depthTex_ != 0) {
        glDeleteTextures(1, &depthTex_);
        depthTex_ = 0;
    }
    depthRgbBuf_.clear();
    depthBuf_.clear();
}

void OakStreams::poll() {
    if (!streaming_) return;
    try {
        // --- RGB frame ---
        auto rgbFrame = queueRgb_->tryGet<dai::ImgFrame>();
        if (rgbFrame) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glBindTexture(GL_TEXTURE_2D, rgbTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                1280, 720,
                GL_RGB, GL_UNSIGNED_BYTE,
                rgbFrame->getData().data());
            glBindTexture(GL_TEXTURE_2D, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }

        // --- Depth frame ---
        auto depthFrame = queueDepth_->tryGet<dai::ImgFrame>();
        if (depthFrame) {
            const auto& data = depthFrame->getData();
            const uint16_t* depthData = reinterpret_cast<const uint16_t*>(data.data());
            const int pixelCount = 640 * 400;

            // Copy raw depth values for histogram access
            std::copy(depthData, depthData + pixelCount, depthBuf_.data());

            // Apply Turbo colormap: close (400mm) → idx 255 (red), far (10000mm) → idx 0 (purple)
            uint8_t* rgbBuf = depthRgbBuf_.data();
            for (int i = 0; i < pixelCount; ++i) {
                uint16_t d = depthData[i];
                if (d == 0) {
                    // Invalid pixel — black
                    rgbBuf[i*3] = rgbBuf[i*3+1] = rgbBuf[i*3+2] = 0;
                    continue;
                }
                int idx = std::clamp(
                    (int)((10000.0f - d) / (10000.0f - 400.0f) * 255.0f),
                    0, 255);
                rgbBuf[i*3]   = kTurboLut[idx][0];
                rgbBuf[i*3+1] = kTurboLut[idx][1];
                rgbBuf[i*3+2] = kTurboLut[idx][2];
            }

            glBindTexture(GL_TEXTURE_2D, depthTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                640, 400,
                GL_RGB, GL_UNSIGNED_BYTE,
                depthRgbBuf_.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "OakStreams::poll error: %s\n", e.what());
        streaming_ = false;
    } catch (...) {
        streaming_ = false;
    }
}

} // namespace oak
```

- [ ] **Step 4.2: Build to check OakStreams compiles**

```bash
cmake --preset windows-debug
cmake --build out/build/windows-debug --config Debug
```

Expected: errors about `Sidebar.cpp`, `StreamView.cpp`, and `main.cpp` still referencing `CameraStream`. OakStreams.cpp itself should show 0 errors. If there are errors in OakStreams.cpp, fix before continuing.

- [ ] **Step 4.3: Commit**

```bash
git add src/oak/OakStreams.cpp
git commit -m "feat: implement OakStreams — RGB+depth pipeline, Turbo LUT, two GL textures"
```

---

## Task 5: Write DepthView.h and DepthView.cpp

**Files:**
- Create: `src/ui/DepthView.h`
- Create: `src/ui/DepthView.cpp`

- [ ] **Step 5.1: Create DepthView.h**

```cpp
// src/ui/DepthView.h
#pragma once
#include "oak/OakStreams.h"

namespace ui {

// Renders the Depth ImGui window: heatmap image above, ImPlot histogram below.
// Call inside an active ImGui frame, after streams.poll().
void renderDepthView(const oak::OakStreams& streams);

} // namespace ui
```

- [ ] **Step 5.2: Create DepthView.cpp**

```cpp
// src/ui/DepthView.cpp
#include "ui/DepthView.h"
#include <imgui.h>
#include <implot.h>

#include <vector>

namespace ui {

void renderDepthView(const oak::OakStreams& streams) {
    ImGui::Begin("Depth");

    if (streams.isStreaming()) {
        // --- Heatmap image (aspect-fit to window width) ---
        float avail  = ImGui::GetContentRegionAvail().x;
        float aspect = (float)streams.depthHeight() / (float)streams.depthWidth();
        ImGui::Image(
            (ImTextureID)(uintptr_t)streams.depthTextureId(),
            ImVec2(avail, avail * aspect)
        );

        // --- ImPlot histogram ---
        // Build vector of valid (non-zero) depth values for this frame
        const uint16_t* raw    = streams.rawDepthData();
        const int       count  = streams.depthPixelCount();

        std::vector<float> validDepths;
        validDepths.reserve(count);
        for (int i = 0; i < count; ++i) {
            if (raw[i] > 0) {
                validDepths.push_back((float)raw[i]);
            }
        }

        if (ImPlot::BeginPlot("Depth Distribution", ImVec2(-1, 160))) {
            ImPlot::SetupAxes("Depth (mm)", "Pixels");
            ImPlot::SetupAxesLimits(400.0, 10000.0, 0, ImPlotCond_Once);
            if (!validDepths.empty()) {
                ImPlot::PlotHistogram("##depth",
                    validDepths.data(),
                    (int)validDepths.size(),
                    100,                // 100 bins
                    1.0,                // bar scale
                    ImPlotRange(400.0, 10000.0));
            }
            ImPlot::EndPlot();
        }

    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("No stream");
        ImGui::TextDisabled("Device not connected or pipeline failed.");
    }

    ImGui::End();
}

} // namespace ui
```

- [ ] **Step 5.3: Build check**

```bash
cmake --build out/build/windows-debug --config Debug
```

Expected: same `CameraStream`-related errors in Sidebar/StreamView/main — DepthView.cpp itself should show 0 errors.

- [ ] **Step 5.4: Commit**

```bash
git add src/ui/DepthView.h src/ui/DepthView.cpp
git commit -m "feat: implement DepthView — depth heatmap + ImPlot histogram"
```

---

## Task 6: Update Sidebar (type swap)

**Files:**
- Modify: `src/ui/Sidebar.h`
- Modify: `src/ui/Sidebar.cpp`

- [ ] **Step 6.1: Update Sidebar.h**

Replace the entire file with:

```cpp
// src/ui/Sidebar.h
#pragma once
#include "oak/Device.h"
#include "oak/OakStreams.h"

namespace ui {

// Renders the left sidebar. Call inside an active ImGui frame.
void renderSidebar(const oak::OakDevice& device, const oak::OakStreams& streams);

} // namespace ui
```

- [ ] **Step 6.2: Update Sidebar.cpp — change parameter type only**

Open `src/ui/Sidebar.cpp`. Make three changes:

**Change 1** — update the include at the top. Replace:
```cpp
#include "oak/CameraStream.h"
```
With:
```cpp
#include "oak/OakStreams.h"
```

**Change 2** — update the function signature. Replace:
```cpp
void renderSidebar(const oak::OakDevice& device, const oak::CameraStream& stream) {
```
With:
```cpp
void renderSidebar(const oak::OakDevice& device, const oak::OakStreams& streams) {
```

**Change 3** — update the `isStreaming()` call in the State block. Replace:
```cpp
if (stream.isStreaming()) {
```
With:
```cpp
if (streams.isStreaming()) {
```

No other changes to Sidebar.cpp — the rest of the streaming logic is unchanged.

- [ ] **Step 6.3: Build check**

```bash
cmake --build out/build/windows-debug --config Debug
```

Expected: errors still in StreamView.cpp and main.cpp — Sidebar should show 0 errors.

- [ ] **Step 6.4: Commit**

```bash
git add src/ui/Sidebar.h src/ui/Sidebar.cpp
git commit -m "feat: update Sidebar — CameraStream → OakStreams parameter"
```

---

## Task 7: Update StreamView (type swap + accessor rename)

**Files:**
- Modify: `src/ui/StreamView.h`
- Modify: `src/ui/StreamView.cpp`

- [ ] **Step 7.1: Update StreamView.h**

Replace the entire file with:

```cpp
// src/ui/StreamView.h
#pragma once
#include "oak/OakStreams.h"

namespace ui {

// Renders the Camera ImGui window (RGB stream).
// Call inside an active ImGui frame, after streams.poll().
void renderStreamView(const oak::OakStreams& streams);

} // namespace ui
```

- [ ] **Step 7.2: Update StreamView.cpp**

Replace the entire file with:

```cpp
// src/ui/StreamView.cpp
#include "ui/StreamView.h"
#include <imgui.h>

namespace ui {

void renderStreamView(const oak::OakStreams& streams) {
    ImGui::Begin("Camera");

    if (streams.isStreaming()) {
        float avail  = ImGui::GetContentRegionAvail().x;
        float aspect = (float)streams.rgbHeight() / (float)streams.rgbWidth();
        ImGui::Image(
            (ImTextureID)(uintptr_t)streams.rgbTextureId(),
            ImVec2(avail, avail * aspect)
        );
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("No stream");
        ImGui::TextDisabled("Device not connected or pipeline failed.");
    }

    ImGui::End();
}

} // namespace ui
```

Changes from Stage 2:
- `CameraStream` → `OakStreams`
- `stream.textureId()` → `streams.rgbTextureId()`
- `stream.width()` → `streams.rgbWidth()`
- `stream.height()` → `streams.rgbHeight()`

- [ ] **Step 7.3: Build check**

```bash
cmake --build out/build/windows-debug --config Debug
```

Expected: errors only in main.cpp now — StreamView should show 0 errors.

- [ ] **Step 7.4: Commit**

```bash
git add src/ui/StreamView.h src/ui/StreamView.cpp
git commit -m "feat: update StreamView — CameraStream → OakStreams, renamed accessors"
```

---

## Task 8: Update main.cpp and delete CameraStream

**Files:**
- Modify: `src/main.cpp`
- Delete: `src/oak/CameraStream.h`
- Delete: `src/oak/CameraStream.cpp`

- [ ] **Step 8.1: Replace main.cpp**

Replace the entire file with:

```cpp
// src/main.cpp
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <implot.h>

#include "oak/Device.h"
#include "oak/OakStreams.h"
#include "ui/Sidebar.h"
#include "ui/StreamView.h"
#include "ui/DepthView.h"

#include <cstdio>

static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int main() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return 1;

    // OpenGL 3.3 core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "oak-explorer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();    // must be after ImGui::CreateContext()
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // OpenGL context is now active — safe to create GL resources
    oak::OakDevice  device;
    oak::OakStreams  streams;
    streams.open();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Poll device state and frames — before ImGui frame
        device.poll();
        streams.poll();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-screen dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_PassthruCentralNode);

        // Render UI panels
        ui::renderSidebar(device, streams);
        ui::renderStreamView(streams);
        ui::renderDepthView(streams);

        // Render and present
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup — GL resources must be freed while context is still active
    streams.close();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();   // before ImGui::DestroyContext()
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
```

- [ ] **Step 8.2: Delete CameraStream files**

```bash
rm src/oak/CameraStream.h
rm src/oak/CameraStream.cpp
```

- [ ] **Step 8.3: Full build — expect 0 errors**

```bash
cmake --preset windows-debug
cmake --build out/build/windows-debug --config Debug
```

Expected: **0 errors**. If there are errors:
- `cannot open include file 'implot.h'` → verify `third_party/implot/implot.h` exists and IMPLOT_DIR is set in CMakeLists.txt
- `'CreateContext': is not a member of 'ImPlot'` → implot.cpp/implot_items.cpp not in sources
- `'rgbTextureId': is not a member of...` → StreamView.cpp still references old CameraStream
- Any linker error about CameraStream → CMakeLists.txt still has CameraStream.cpp in sources

- [ ] **Step 8.4: Commit**

```bash
git add src/main.cpp
git rm src/oak/CameraStream.h src/oak/CameraStream.cpp
git commit -m "feat: wire OakStreams + ImPlot into main; remove CameraStream"
```

---

## Task 9: Milestone verification

**No file changes — runtime testing only.**

- [ ] **Step 9.1: Launch with OAK-D-Lite connected**

```bash
out\build\windows-debug\Debug\oak-explorer.exe
```

Expected output in stderr:
```
OakStreams: opened — RGB 1280x720, Depth 640x400 @ 30fps
```

Expected in window:
- Sidebar: green "Streaming" label
- "Camera" window: live RGB feed, 16:9 aspect ratio
- "Depth" window: Turbo heatmap of scene (close objects red/orange, far objects blue/purple)
- Below heatmap: histogram showing distribution of depth values

- [ ] **Step 9.2: Verify colormap direction**

Hold your hand close to the camera (~40-50cm). The hand should appear red/orange in the depth window. Objects further away should appear blue/purple. If the colors are inverted (hand appears purple, far objects red), the colormap formula is wrong.

- [ ] **Step 9.3: Verify histogram updates**

Move your hand toward and away from the camera. The histogram should shift left (closer = lower mm value) and right (farther = higher mm value) accordingly. The histogram should only show the distribution of valid pixels (depth > 0) — not a spike at zero.

- [ ] **Step 9.4: Verify no-device launch**

Unplug the OAK. Run again.

Expected:
- App launches without crashing
- `OakStreams::open failed: ...` logged to stderr
- Sidebar: "No device" (red)
- "Camera" window: "No stream"
- "Depth" window: "No stream"

- [ ] **Step 9.5: Final commit and tag**

```bash
git add -A
git commit -m "feat: stage 3 complete — depth heatmap, Turbo colormap, ImPlot histogram"
git tag stage-3-complete
git push
git push origin stage-3-complete
```

---

## Success Criteria (from spec)

- App runs with OAK-D-Lite: RGB window shows live color feed, Depth window shows Turbo heatmap, histogram updates each frame
- App runs without device: both windows show "No stream" placeholder, no crash
- Frame rate: ≥ 25fps in debug build (both streams active)
- Colormap: close objects appear red/orange, far objects appear blue/purple
- Histogram: shows mm distribution for valid pixels only, updates live
