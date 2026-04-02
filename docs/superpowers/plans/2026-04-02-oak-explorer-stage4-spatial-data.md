# Stage 4 — Spatial Data: Click to Measure — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user click (or hover) on the Camera window to sample a real-world XYZ position in metres, displayed colour-coded in the sidebar.

**Architecture:** `OakStreams` gains a `SpatialLocationCalculator` node (wired to the existing `StereoDepth` output) and an `XLinkIn` node for host→device config updates. `setRoi(normX, normY)` sends a 5%×5% ROI config packet to the device; `poll()` drains the resulting `SpatialLocationData` packets. `StreamView` handles mouse input and draws a corner-bracket reticle via `ImDrawList`. `Sidebar` shows a Lock/Track toggle and colour-coded X/Y/Z readout. A `SpatialMode` enum in its own header is shared by both UI files.

**Tech Stack:** depthai-core v2.32.0, C++17, ImGui docking branch, GLFW, VS2026 Insiders

---

## Context for implementers

- Working directory: `E:\luxonis\oak-explorer\`
- Build command (use terminal — VS2026 has a bug where MSBuild re-runs lose cacheVariables):
  ```bash
  cmake --preset windows-debug
  cmake --build out/build/windows-debug --config Debug
  ```
- Run: `out\build\windows-debug\Debug\oak-explorer.exe`
- Stage 3 is complete — `OakStreams` already has RGB + StereoDepth pipeline, two GL textures, poll loop
- **No unit test framework.** Verification = build succeeds (0 errors) + visual/runtime confirmation
- Device must be plugged in via USB3 for runtime testing
- Use `#include <GLFW/glfw3.h>` for OpenGL — never `<GL/GL.h>` (WINGDIAPI conflict with depthai)

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/ui/SpatialMode.h` | Create | `ui::SpatialMode` enum shared by StreamView and Sidebar |
| `src/oak/OakStreams.h` | Modify | Add spatial private members + public interface |
| `src/oak/OakStreams.cpp` | Modify | Add spatial nodes to pipeline; implement `setRoi()`; drain spatial queue in `poll()`; reset in `close()` |
| `src/ui/StreamView.h` | Modify | Add `SpatialMode` param; make `streams` non-const |
| `src/ui/StreamView.cpp` | Modify | Mouse click/hover → `setRoi()`; corner-bracket reticle via `ImDrawList` |
| `src/ui/Sidebar.h` | Modify | Add `SpatialMode&` param |
| `src/ui/Sidebar.cpp` | Modify | Mode radio buttons + colour-coded XYZ readout |
| `src/main.cpp` | Modify | Add `SpatialMode` local var; pass to both render calls |

---

## Task 1: Create SpatialMode header

**Files:**
- Create: `src/ui/SpatialMode.h`

- [ ] **Step 1.1: Create the header**

```cpp
// src/ui/SpatialMode.h
#pragma once

namespace ui {

// Controls how the Camera window samples a spatial point.
// Lock  — click to fix the ROI; it stays until the next click.
// Track — ROI follows the mouse continuously while hovering.
enum class SpatialMode { Lock, Track };

} // namespace ui
```

- [ ] **Step 1.2: Commit**

```bash
git add src/ui/SpatialMode.h
git commit -m "feat: add SpatialMode enum (Lock/Track)"
```

---

## Task 2: Update OakStreams.h — add spatial interface

**Files:**
- Modify: `src/oak/OakStreams.h`

- [ ] **Step 2.1: Add `DataInputQueue` forward-declare and spatial members**

The current header forward-declares `dai::DataOutputQueue`. Add `dai::DataInputQueue` alongside it, then extend the class.

Replace the forward-declare block:
```cpp
// Forward-declare depthai types used in private members
namespace dai {
    class Device;
    class DataOutputQueue;
}
```
With:
```cpp
// Forward-declare depthai types used in private members
namespace dai {
    class Device;
    class DataOutputQueue;
    class DataInputQueue;
}
```

- [ ] **Step 2.2: Add public spatial accessors after `depthPixelCount()`**

Find the line:
```cpp
    int             depthPixelCount() const { return depthWidth() * depthHeight(); }
```

Add after it:
```cpp
    // Send an updated ROI to the SpatialLocationCalculator.
    // normX, normY are in [0,1] relative to the 1280×720 RGB image.
    // ROI is fixed at 5%×5% of the frame (±0.025 around the centre).
    // Safe to call every frame in Track mode — sends only when streaming.
    void setRoi(float normX, float normY);

    bool  hasSpatial()  const { return hasSpatial_; }
    float spatialX()    const { return spatialX_; }   // metres, right of centre
    float spatialY()    const { return spatialY_; }   // metres, down from centre
    float spatialZ()    const { return spatialZ_; }   // metres, forward (depth)
    float roiCenterX()  const { return roiCenterX_; } // normalised [0,1]
    float roiCenterY()  const { return roiCenterY_; } // normalised [0,1]
```

- [ ] **Step 2.3: Add private spatial members**

Find the existing private members block:
```cpp
    int     droppedRgb_   = 0;
    int     droppedDepth_ = 0;
    int64_t lastRgbSeq_   = -1;  // -1 = no frame seen yet
    int64_t lastDepthSeq_ = -1;

    bool streaming_ = false;
```

Replace with:
```cpp
    int     droppedRgb_   = 0;
    int     droppedDepth_ = 0;
    int64_t lastRgbSeq_   = -1;  // -1 = no frame seen yet
    int64_t lastDepthSeq_ = -1;

    std::shared_ptr<dai::DataInputQueue>  queueSpatialConfig_;
    std::shared_ptr<dai::DataOutputQueue> queueSpatial_;
    float spatialX_    = 0.0f;
    float spatialY_    = 0.0f;
    float spatialZ_    = 0.0f;
    float roiCenterX_  = 0.5f;  // starts at image centre
    float roiCenterY_  = 0.5f;
    bool  hasSpatial_  = false;

    bool streaming_ = false;
```

- [ ] **Step 2.4: Build — expect linker/compile errors only from missing .cpp implementations**

```bash
cmake --build out/build/windows-debug --config Debug
```

Expected: errors about `setRoi` not defined. That is correct — the header change is clean.

- [ ] **Step 2.5: Commit**

```bash
git add src/oak/OakStreams.h
git commit -m "feat: add spatial interface to OakStreams header"
```

---

## Task 3: Update OakStreams.cpp — spatial pipeline + setRoi() + poll() + close()

**Files:**
- Modify: `src/oak/OakStreams.cpp`

- [ ] **Step 3.1: Add spatial nodes to the pipeline in `open()`**

Find the line in `open()`:
```cpp
        // --- Open device (boots pipeline onto VPU) ---
        device_ = std::make_unique<dai::Device>(pipeline);
```

Insert the following block **immediately before** that line:

```cpp
        // --- SpatialLocationCalculator ---
        // Wired to stereo depth; host sends ROI configs via XLinkIn.
        auto spatialCalc = pipeline.create<dai::node::SpatialLocationCalculator>();
        spatialCalc->setWaitForConfigInput(false);  // use initialConfig until first update

        // Initial ROI at image centre (5%×5%)
        dai::SpatialLocationCalculatorConfigData initialRoi;
        initialRoi.roi = dai::Rect(dai::Point2f(0.475f, 0.475f),
                                   dai::Point2f(0.525f, 0.525f));
        initialRoi.calculationAlgorithm =
            dai::SpatialLocationCalculatorAlgorithm::MEDIAN;
        spatialCalc->initialConfig.addROI(initialRoi);

        stereo->depth.link(spatialCalc->inputDepth);

        // XLinkIn — receives SpatialLocationsConfig packets from host
        auto spatialConfigIn = pipeline.create<dai::node::XLinkIn>();
        spatialConfigIn->setStreamName("spatialConfig");
        spatialConfigIn->out.link(spatialCalc->inputConfig);

        // XLinkOut — sends SpatialLocationData packets to host
        auto xoutSpatial = pipeline.create<dai::node::XLinkOut>();
        xoutSpatial->setStreamName("spatial");
        spatialCalc->out.link(xoutSpatial->input);

```

- [ ] **Step 3.2: Get spatial queues after device opens**

Find:
```cpp
        queueRgb_   = device_->getOutputQueue("rgb",   4, false);
        queueDepth_ = device_->getOutputQueue("depth", 4, false);
```

Add after those two lines:
```cpp
        queueSpatial_       = device_->getOutputQueue("spatial",       4, false);
        queueSpatialConfig_ = device_->getInputQueue("spatialConfig");
```

- [ ] **Step 3.3: Implement `setRoi()` — add before `open()`**

Add this function after the closing brace of the Turbo LUT table and before `OakStreams::OakStreams()`:

```cpp
void OakStreams::setRoi(float normX, float normY) {
    if (!streaming_) return;

    // Clamp to valid range — keep the 5% half-width inside the frame
    normX = std::clamp(normX, 0.025f, 0.975f);
    normY = std::clamp(normY, 0.025f, 0.975f);
    roiCenterX_ = normX;
    roiCenterY_ = normY;

    dai::SpatialLocationCalculatorConfig cfg;
    dai::SpatialLocationCalculatorConfigData roi;
    roi.roi = dai::Rect(dai::Point2f(normX - 0.025f, normY - 0.025f),
                        dai::Point2f(normX + 0.025f, normY + 0.025f));
    roi.calculationAlgorithm = dai::SpatialLocationCalculatorAlgorithm::MEDIAN;
    cfg.addROI(roi);

    try {
        queueSpatialConfig_->send(cfg);
    } catch (...) {
        // Non-fatal — spatial result simply won't update this frame
    }
}
```

- [ ] **Step 3.4: Drain spatial queue in `poll()`**

Find the end of the depth frame block in `poll()`:

```cpp
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    } catch (const std::exception& e) {
```

Insert after the closing `}` of the depth `if (depthFrame)` block, still inside the outer `try`:

```cpp
        // --- Spatial location result ---
        auto spatialData = queueSpatial_->tryGet<dai::SpatialLocationCalculatorData>();
        if (spatialData) {
            const auto locs = spatialData->getSpatialLocations();
            if (!locs.empty()) {
                const auto& coords = locs[0].spatialCoordinates;
                spatialX_   = coords.x / 1000.0f;  // mm → metres
                spatialY_   = coords.y / 1000.0f;
                spatialZ_   = coords.z / 1000.0f;
                hasSpatial_ = true;
            }
        }
```

- [ ] **Step 3.5: Reset spatial state in `close()`**

Find in `close()`:
```cpp
    queueRgb_.reset();    // release queues before device
    queueDepth_.reset();
    device_.reset();      // destructor stops pipeline
```

Replace with:
```cpp
    queueRgb_.reset();           // release queues before device
    queueDepth_.reset();
    queueSpatial_.reset();
    queueSpatialConfig_.reset();
    device_.reset();             // destructor stops pipeline
    hasSpatial_  = false;
    roiCenterX_  = 0.5f;
    roiCenterY_  = 0.5f;
```

- [ ] **Step 3.6: Build — expect 0 errors in OakStreams**

```bash
cmake --build out/build/windows-debug --config Debug
```

Expected: OakStreams compiles cleanly. Errors from StreamView/Sidebar (signatures not yet updated) are expected.

- [ ] **Step 3.7: Commit**

```bash
git add src/oak/OakStreams.h src/oak/OakStreams.cpp
git commit -m "feat: add SpatialLocationCalculator to OakStreams pipeline"
```

---

## Task 4: Update StreamView — mouse input + corner-bracket reticle

**Files:**
- Modify: `src/ui/StreamView.h`
- Modify: `src/ui/StreamView.cpp`

- [ ] **Step 4.1: Update StreamView.h**

Replace the entire file with:

```cpp
// src/ui/StreamView.h
#pragma once
#include "oak/OakStreams.h"
#include "ui/SpatialMode.h"

namespace ui {

// Renders the Camera ImGui window (RGB stream).
// Handles mouse input for spatial ROI sampling.
// streams is non-const — calls streams.setRoi() on click/hover.
// Call inside an active ImGui frame, after streams.poll().
void renderStreamView(oak::OakStreams& streams, SpatialMode mode);

} // namespace ui
```

- [ ] **Step 4.2: Update StreamView.cpp**

Replace the entire file with:

```cpp
// src/ui/StreamView.cpp
#include "ui/StreamView.h"
#include <imgui.h>
#include <algorithm>  // std::clamp

namespace ui {

// Draws a corner-bracket reticle centred at (cx, cy) in screen space.
static void drawReticle(ImDrawList* dl, float cx, float cy) {
    const float half = 12.0f;  // half-size of bracket box
    const float arm  =  7.0f;  // length of each bracket arm
    const float lw   =  1.5f;  // line width
    const ImU32 col  = IM_COL32(255, 255, 255, 210);

    // Top-left corner
    dl->AddLine(ImVec2(cx - half, cy - half + arm), ImVec2(cx - half, cy - half), col, lw);
    dl->AddLine(ImVec2(cx - half, cy - half),       ImVec2(cx - half + arm, cy - half), col, lw);
    // Top-right corner
    dl->AddLine(ImVec2(cx + half - arm, cy - half), ImVec2(cx + half, cy - half), col, lw);
    dl->AddLine(ImVec2(cx + half, cy - half),       ImVec2(cx + half, cy - half + arm), col, lw);
    // Bottom-left corner
    dl->AddLine(ImVec2(cx - half, cy + half - arm), ImVec2(cx - half, cy + half), col, lw);
    dl->AddLine(ImVec2(cx - half, cy + half),       ImVec2(cx - half + arm, cy + half), col, lw);
    // Bottom-right corner
    dl->AddLine(ImVec2(cx + half - arm, cy + half), ImVec2(cx + half, cy + half), col, lw);
    dl->AddLine(ImVec2(cx + half, cy + half),       ImVec2(cx + half, cy + half - arm), col, lw);
    // Centre dot
    dl->AddCircleFilled(ImVec2(cx, cy), 2.5f, col);
}

void renderStreamView(oak::OakStreams& streams, SpatialMode mode) {
    ImGui::SetNextWindowPos(ImVec2(240, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900, 540), ImGuiCond_FirstUseEver);
    ImGui::Begin("Camera");

    if (streams.isStreaming()) {
        float avail  = ImGui::GetContentRegionAvail().x;
        float aspect = (float)streams.rgbHeight() / (float)streams.rgbWidth();
        ImVec2 imageSize(avail, avail * aspect);

        ImGui::Image(
            (ImTextureID)(uintptr_t)streams.rgbTextureId(),
            imageSize
        );

        // Item rect is valid immediately after Image()
        ImVec2 imageMin = ImGui::GetItemRectMin();
        ImVec2 imageMax = ImGui::GetItemRectMax();
        float  imgW     = imageMax.x - imageMin.x;
        float  imgH     = imageMax.y - imageMin.y;

        // --- Mouse input ---
        if (ImGui::IsItemHovered()) {
            ImVec2 mouse = ImGui::GetMousePos();
            float normX = std::clamp((mouse.x - imageMin.x) / imgW, 0.0f, 1.0f);
            float normY = std::clamp((mouse.y - imageMin.y) / imgH, 0.0f, 1.0f);

            if (mode == SpatialMode::Track) {
                streams.setRoi(normX, normY);
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                streams.setRoi(normX, normY);
            }
        }

        // --- Corner-bracket reticle at current ROI centre ---
        float cx = imageMin.x + streams.roiCenterX() * imgW;
        float cy = imageMin.y + streams.roiCenterY() * imgH;
        drawReticle(ImGui::GetWindowDrawList(), cx, cy);

    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("No stream");
        ImGui::TextDisabled("Device not connected or pipeline failed.");
    }

    ImGui::End();
}

} // namespace ui
```

- [ ] **Step 4.3: Build — expect errors only from Sidebar and main (not yet updated)**

```bash
cmake --build out/build/windows-debug --config Debug
```

Expected: StreamView compiles cleanly. Errors in Sidebar.cpp (old signature) and main.cpp are expected.

- [ ] **Step 4.4: Commit**

```bash
git add src/ui/StreamView.h src/ui/StreamView.cpp
git commit -m "feat: StreamView — mouse ROI sampling + corner-bracket reticle"
```

---

## Task 5: Update Sidebar — mode toggle + XYZ readout

**Files:**
- Modify: `src/ui/Sidebar.h`
- Modify: `src/ui/Sidebar.cpp`

- [ ] **Step 5.1: Update Sidebar.h**

Replace the entire file with:

```cpp
// src/ui/Sidebar.h
#pragma once
#include "oak/Device.h"
#include "oak/OakStreams.h"
#include "ui/SpatialMode.h"

namespace ui {

// Renders the left sidebar. Call inside an active ImGui frame.
// mode is read-write — the sidebar shows the toggle and the user can change it.
void renderSidebar(const oak::OakDevice& device,
                   const oak::OakStreams& streams,
                   SpatialMode& mode);

} // namespace ui
```

- [ ] **Step 5.2: Update Sidebar.cpp**

Replace the entire file with:

```cpp
// src/ui/Sidebar.cpp
#include "ui/Sidebar.h"
#include "oak/OakStreams.h"
#include <imgui.h>

namespace ui {

void renderSidebar(const oak::OakDevice& device,
                   const oak::OakStreams& streams,
                   SpatialMode& mode) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(220, ImGui::GetIO().DisplaySize.y), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Device", nullptr, flags);

    // Connection status indicator
    // Note: ImGui's default font (ProggyClean) is ASCII-only — use ImGui::Bullet()
    // instead of Unicode bullet characters like ●
    if (device.isConnected()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::Text("Connected");
        ImGui::PopStyleColor();

        const auto& info = device.info();
        ImGui::Spacing();

        ImGui::TextDisabled("MX ID");
        ImGui::TextWrapped("%s", info.mxId.c_str());
        ImGui::Spacing();

        ImGui::TextDisabled("State");
        if (streams.isStreaming()) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Streaming");
            ImGui::Spacing();

            ImGui::TextDisabled("Dropped frames");
            ImGui::Text("RGB:   %d", streams.droppedRgbFrames());
            ImGui::Text("Depth: %d", streams.droppedDepthFrames());
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                info.available ? "Unbooted (ready)" : "Booted / in use");
            if (!info.available) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "(claimed by a process)");
            }
        }
        ImGui::Spacing();

        // --- Spatial mode + readout (only meaningful when streaming) ---
        if (streams.isStreaming()) {
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextDisabled("Spatial mode");
            bool lockSelected  = (mode == SpatialMode::Lock);
            bool trackSelected = (mode == SpatialMode::Track);
            if (ImGui::RadioButton("Lock",  lockSelected))  mode = SpatialMode::Lock;
            if (ImGui::RadioButton("Track", trackSelected)) mode = SpatialMode::Track;
            ImGui::Spacing();

            ImGui::TextDisabled("Sample point");
            if (streams.hasSpatial()) {
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f),
                    "X  %+.2f m", streams.spatialX());
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
                    "Y  %+.2f m", streams.spatialY());
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                    "Z  %+.2f m", streams.spatialZ());
            } else {
                ImGui::TextDisabled("Click Camera to sample");
            }
            ImGui::Spacing();
        }

    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::Text("No device");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextDisabled("Connect OAK-D-Lite");
        ImGui::TextDisabled("via USB3 and relaunch.");
    }

    // Performance stats — pinned to the bottom of the sidebar
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 56.0f);
    ImGui::Separator();
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
#ifdef NDEBUG
    ImGui::TextDisabled("RELEASE");
#else
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "DEBUG");
#endif

    ImGui::End();
}

} // namespace ui
```

- [ ] **Step 5.3: Build — expect errors only from main.cpp (old renderSidebar/renderStreamView calls)**

```bash
cmake --build out/build/windows-debug --config Debug
```

Expected: Sidebar compiles cleanly. Only main.cpp should error.

- [ ] **Step 5.4: Commit**

```bash
git add src/ui/Sidebar.h src/ui/Sidebar.cpp
git commit -m "feat: Sidebar — SpatialMode toggle + colour-coded XYZ readout"
```

---

## Task 6: Update main.cpp — wire SpatialMode through

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 6.1: Update main.cpp**

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
#include "ui/SpatialMode.h"

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
    device.poll();  // capture MX ID + connected state before pipeline boots
    streams.open();

    ui::SpatialMode spatialMode = ui::SpatialMode::Lock;  // default: click to lock

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Poll device state and frames — before ImGui frame
        // Skip USB enumeration while streaming: getAllConnectedDevices() blocks
        // ~1s on Windows with an active pipeline, tanking frame rate.
        // streams.poll() will set streaming_=false if the device drops,
        // which re-enables device.poll() on the next frame.
        if (!streams.isStreaming()) device.poll();
        streams.poll();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-screen dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_PassthruCentralNode);

        // Render UI panels
        ui::renderSidebar(device, streams, spatialMode);
        ui::renderStreamView(streams, spatialMode);
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

- [ ] **Step 6.2: Build — expect 0 errors**

```bash
cmake --preset windows-debug
cmake --build out/build/windows-debug --config Debug
```

Expected: **0 errors, 0 warnings** (or only innocuous depthai warnings).
If there are errors, read them carefully — most likely a type name mismatch or missing include.

- [ ] **Step 6.3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire SpatialMode through main render loop"
```

---

## Task 7: Milestone verification

**No file changes — runtime testing only.**

- [ ] **Step 7.1: Launch with device plugged in**

Run: `out\build\windows-debug\Debug\oak-explorer.exe`

Expected on startup:
- Window opens, streams start as before
- Sidebar shows "Spatial mode" radio buttons (Lock selected by default)
- Sidebar shows "Click Camera to sample" under "Sample point"
- Corner-bracket reticle appears at the centre of the Camera image

- [ ] **Step 7.2: Test Lock mode**

Click anywhere on the Camera image.

Expected:
- Reticle jumps to the clicked position and stays there
- Sidebar shows X / Y / Z in metres within ~1 second (the OAK needs one result cycle)
- Numbers are colour-coded: X blue, Y green, Z orange

- [ ] **Step 7.3: Verify Z with a ruler**

Point the camera at a flat wall or object at a known distance (e.g. exactly 1.0 m). Click on it.

Expected: `Z  +1.00 m` (±5–10 cm is normal for stereo depth at 1 m).

- [ ] **Step 7.4: Test Track mode**

Switch to "Track" in the sidebar. Move the mouse over the Camera window.

Expected:
- Reticle follows the mouse smoothly
- X/Y/Z update continuously as you move
- Z increases as you point toward far objects, decreases toward near ones

- [ ] **Step 7.5: Verify coordinate directions**

With the camera facing forward:
- Move the mouse right → X goes more positive
- Move the mouse down → Y goes more positive
- Point at something closer → Z decreases

- [ ] **Step 7.6: Final commit and tag**

```bash
git add -A
git commit -m "feat: stage 4 complete — click-to-measure spatial XYZ in metres"
git tag stage-4-complete
git push
git push origin stage-4-complete
```
