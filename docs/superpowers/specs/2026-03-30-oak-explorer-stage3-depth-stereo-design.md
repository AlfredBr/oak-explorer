# Stage 3 — Depth & Stereo Implementation Design

> **For agentic workers:** Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` to implement this spec task-by-task.

**Goal:** Add a live depth heatmap alongside the RGB stream — StereoDepth pipeline, Turbo colormap applied CPU-side, depth texture uploaded to OpenGL, rendered in a separate ImGui window with an ImPlot histogram below it.

**Architecture:** Replace `CameraStream` with `OakStreams` — a single class that owns the one `dai::Device`, builds a combined pipeline (RGB + stereo depth), and exposes two GL textures plus the raw depth buffer for histogram rendering. Two new UI panels: `DepthView` (heatmap image + histogram) alongside the existing `StreamView` (RGB).

**Tech Stack:** depthai-core 2.32.0, OpenGL 3.3, ImGui docking branch, ImPlot v0.16 (new, vendored), GLFW 3.4, MSVC C++17

---

## Design Decisions

### One device, one pipeline
The OAK camera can only be opened by one `dai::Device` instance. All nodes — ColorCamera, MonoCamera (left), MonoCamera (right), StereoDepth — must be added to a single `dai::Pipeline` before `dai::Device` is constructed. `OakStreams` owns this device and all downstream queues.

### OakStreams replaces CameraStream
`CameraStream` handled RGB only. `OakStreams` handles both streams with a unified interface: one `open()`, one `close()`, one `poll()` that drains both queues. It exposes `rgbTextureId()` and `depthTextureId()` as `GLuint`, and `rawDepthData()` / `depthPixelCount()` for histogram access. `StreamView` and `Sidebar` are updated to take `const OakStreams&` instead of `const CameraStream&`.

### OakDevice and OakStreams coexist without conflict
`OakDevice::poll()` enumerates devices but does not open them. Once `OakStreams::open()` boots the pipeline, `OakDevice` will see the device as unavailable (`available == false`). The existing `Sidebar.cpp` already handles this correctly: it checks `streams.isStreaming()` first and shows "Streaming" in green — the OakDevice "Booted / in use" path is never reached while streaming is active. No changes to this logic are needed.

### Colormap: Turbo, CPU-side LUT, close=red far=purple
Depth frames arrive as `uint16` pixels in mm. Each frame, on the CPU: for each pixel, normalize depth to index `[0, 255]` where **0 = far (10000mm) → purple** and **255 = close (400mm) → red**. Look up in the 256-entry Turbo LUT, write into a `uint8_t` staging buffer (640×400×3), upload via `glTexSubImage2D`. Invalid pixels (depth == 0) map to black. The LUT is a `constexpr` array in `OakStreams.cpp`.

Formula (close maps to high index = red end of Turbo):
```cpp
int idx = std::clamp((int)((10000.0f - d) / (10000.0f - 400.0f) * 255.0f), 0, 255);
```

### Raw depth data exposed for histogram
`OakStreams` retains the last depth frame's data in a `std::vector<uint16_t> depthBuf_` member (resized once at open). `poll()` copies the raw uint16 values into it. `DepthView` accesses it via `rawDepthData()` / `depthPixelCount()` to build the ImPlot histogram directly from the live data.

### Depth resolution
OAK-D-Lite mono cameras output 640×400 at 30fps. The depth map from `StereoDepth` matches this resolution. The depth GL texture is 640×400, separate from the 1280×720 RGB texture. 640 × 3 = 1920 bytes per row — 4-byte aligned — so `GL_UNPACK_ALIGNMENT` does not need to be changed for the depth texture (unlike RGB).

### StereoDepth configuration
Use `HIGH_DENSITY` preset (`dai::node::StereoDepth::PresetType::HIGH_DENSITY`). Connect `stereo->depth` (not `stereo->disparity`) to `XLinkOut` — depth is in mm as uint16, which is what we want for colormap normalization.

### ImPlot histogram in DepthView
`DepthView` is one ImGui window containing the depth heatmap image above and an ImPlot histogram below. The histogram shows the distribution of valid depth values (depth > 0) in the current frame, 100 bins over 400–10000mm. Both live in the same window — seeing the heatmap and its histogram together is more useful than a separate window.

### ImPlot vendored in third_party/
Same pattern as ImGui: download ImPlot v0.16, place in `third_party/implot/`. Files needed: `implot.h`, `implot_internal.h`, `implot.cpp`, `implot_items.cpp`. Add sources to `CMakeLists.txt`. `ImPlot::CreateContext()` must be called after `ImGui::CreateContext()` and `ImPlot::DestroyContext()` before `ImGui::DestroyContext()`.

---

## Pipeline

```
Device side (VPU):
  ColorCamera  (preview 1280×720, RGB, interleaved) → XLinkOut("rgb")
  MonoCamera L (400p, left socket)  ─┐
                                      ├→ StereoDepth (HIGH_DENSITY) → XLinkOut("depth")
  MonoCamera R (400p, right socket) ─┘

Host side:
  OakStreams::poll():
    queueRgb_->tryGet<ImgFrame>()   → glTexSubImage2D(rgbTex_)
    queueDepth_->tryGet<ImgFrame>() → copy to depthBuf_ → applyTurboLUT() → glTexSubImage2D(depthTex_)
```

---

## File Structure

```
src/
├── main.cpp                     ← modified (see below)
├── oak/
│   ├── Device.h/cpp             ← unchanged
│   ├── CameraStream.h/cpp       ← DELETED
│   └── OakStreams.h/cpp         ← new: pipeline, device, both textures, Turbo LUT, raw depth buf
└── ui/
    ├── Sidebar.h/cpp            ← modified: CameraStream& → OakStreams&
    ├── StreamView.h/cpp         ← modified: CameraStream& → OakStreams&
    └── DepthView.h/cpp          ← new: depth heatmap + ImPlot histogram window

third_party/
├── imgui/                       ← unchanged
└── implot/                      ← new: implot.h, implot_internal.h, implot.cpp, implot_items.cpp

CMakeLists.txt                   ← see changes below
```

### CMakeLists.txt changes

```cmake
# Remove from add_executable:
#   src/oak/CameraStream.cpp
#   src/ui/StreamView.cpp  (re-add with updated source — same filename, updated content)

# Add to add_executable:
set(IMPLOT_DIR ${CMAKE_SOURCE_DIR}/third_party/implot)

add_executable(oak-explorer
    src/main.cpp
    src/oak/Device.cpp
    src/oak/OakStreams.cpp          # replaces CameraStream.cpp
    src/ui/Sidebar.cpp
    src/ui/StreamView.cpp
    src/ui/DepthView.cpp            # new
    # imgui sources (unchanged) ...
    ${IMPLOT_DIR}/implot.cpp        # new
    ${IMPLOT_DIR}/implot_items.cpp  # new
)

target_include_directories(oak-explorer PRIVATE
    # existing includes ...
    ${IMPLOT_DIR}                   # new: so #include <implot.h> works
)
```

---

## Interface Contracts

### `OakStreams` (`src/oak/OakStreams.h`)

```cpp
namespace oak {

class OakStreams {
public:
    OakStreams();
    ~OakStreams();  // calls close()

    void open();
    void close();
    void poll();   // drains both queues, uploads textures, updates depthBuf_

    bool   isStreaming()    const { return streaming_; }

    // GL textures for rendering
    GLuint rgbTextureId()   const { return rgbTex_; }
    GLuint depthTextureId() const { return depthTex_; }
    int    rgbWidth()       const { return 1280; }
    int    rgbHeight()      const { return 720; }
    int    depthWidth()     const { return 640; }
    int    depthHeight()    const { return 400; }

    // Raw depth data for histogram (uint16, mm, 0=invalid)
    const uint16_t* rawDepthData()  const { return depthBuf_.data(); }
    int             depthPixelCount() const { return depthWidth() * depthHeight(); }

private:
    std::unique_ptr<dai::Device>          device_;
    std::shared_ptr<dai::DataOutputQueue> queueRgb_;
    std::shared_ptr<dai::DataOutputQueue> queueDepth_;

    GLuint rgbTex_   = 0;
    GLuint depthTex_ = 0;

    std::vector<uint8_t>   depthRgbBuf_;   // 640*400*3 — staging buffer for colormap upload
    std::vector<uint16_t>  depthBuf_;      // 640*400   — raw depth values for histogram

    bool streaming_ = false;
};

} // namespace oak
```

### `DepthView` (`src/ui/DepthView.h`)

```cpp
namespace ui {
    void renderDepthView(const oak::OakStreams& streams);
} // namespace ui
```

### Updated signatures

```cpp
// Sidebar.h
void renderSidebar(const oak::OakDevice& device, const oak::OakStreams& streams);

// StreamView.h
void renderStreamView(const oak::OakStreams& streams);
```

---

## main.cpp changes

```cpp
#include "oak/OakStreams.h"     // replaces CameraStream.h
#include "ui/DepthView.h"       // new
#include <implot.h>             // new

// After ImGui::CreateContext() and ImGui::StyleColorsDark():
ImPlot::CreateContext();        // must be after ImGui::CreateContext()

// OpenGL context active — safe to create GL resources:
oak::OakDevice device;
oak::OakStreams streams;        // replaces CameraStream
streams.open();

// Render loop (unchanged structure):
device.poll();
streams.poll();                 // replaces stream.poll()

ui::renderSidebar(device, streams);
ui::renderStreamView(streams);
ui::renderDepthView(streams);   // new

// Cleanup — before ImGui/GLFW teardown:
streams.close();
ImPlot::DestroyContext();       // before ImGui::DestroyContext()
ImGui_ImplOpenGL3_Shutdown();
ImGui_ImplGlfw_Shutdown();
ImGui::DestroyContext();
```

---

## Colormap — Turbo LUT

The Turbo colormap maps **close objects → red (index 255)** and **far objects → purple (index 0)**. Formula uses floating-point to avoid integer truncation:

```cpp
for (int i = 0; i < 640*400; ++i) {
    uint16_t d = depthData[i];
    if (d == 0) {
        // Invalid pixel → black
        rgbBuf[i*3] = rgbBuf[i*3+1] = rgbBuf[i*3+2] = 0;
        continue;
    }
    // close (400mm) → idx 255 (red), far (10000mm) → idx 0 (purple)
    int idx = std::clamp((int)((10000.0f - d) / (10000.0f - 400.0f) * 255.0f), 0, 255);
    rgbBuf[i*3]   = kTurboLut[idx][0];
    rgbBuf[i*3+1] = kTurboLut[idx][1];
    rgbBuf[i*3+2] = kTurboLut[idx][2];
}
```

The 256-entry `kTurboLut` is a `static constexpr uint8_t[256][3]` array in `OakStreams.cpp`. Use the canonical Turbo LUT from Google's Turbo colormap (well-known, widely reproduced).

---

## DepthView layout

```
┌─────────────────────────────────────────┐
│  Depth                                  │
│  ┌─────────────────────────────────┐    │
│  │  640×400 heatmap (aspect-fit)   │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │  ImPlot histogram               │    │
│  │  x: depth (mm)  y: pixel count  │    │
│  │  100 bins, 400–10000mm          │    │
│  │  valid pixels only (depth > 0)  │    │
│  └─────────────────────────────────┘    │
└─────────────────────────────────────────┘
```

`renderDepthView` uses `streams.rawDepthData()` and `streams.depthPixelCount()` to build a `std::vector<float>` of valid depth values each frame, then calls `ImPlot::PlotHistogram`.

---

## Dependencies

| New dependency | Version | Where | How to get |
|---|---|---|---|
| ImPlot | v0.16 | `third_party/implot/` | `curl -L https://github.com/epezent/implot/archive/refs/tags/v0.16.zip` |

ImPlot init/shutdown order relative to ImGui:
```
ImGui::CreateContext()     → ImPlot::CreateContext()
ImPlot::DestroyContext()   → ImGui::DestroyContext()
```

---

## Success Criteria

- App runs with OAK-D-Lite connected: RGB window shows live color feed, Depth window shows Turbo heatmap, histogram updates each frame
- App runs without device: both windows show "No stream" placeholder, no crash
- Frame rate: ≥ 25fps in debug build (both streams active)
- Colormap is correct: **close objects appear red/yellow, far objects appear blue/purple**
- Histogram shows distribution of depth values (mm) in the current frame, updates live, only valid pixels counted
