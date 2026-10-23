# oak-explorer

(This README.md was written 99% by Claude.  I'll rewrite this when the code is in better shape...)

A C++ learning harness for the [Luxonis OAK-D-Lite](https://shop.luxonis.com/products/oak-d-lite) depth camera.

Built with ImGui + OpenGL3 + GLFW. The goal is to build up a single growing application across six stages, understanding every line rather than copying demo code.

---

## Stages

| Stage | Name | Status |
|---|---|---|
| 1 | Foundation — ImGui window, device detection sidebar | ✅ Complete |
| 2 | Camera Streams — live RGB frame as ImGui texture | ✅ Complete |
| 3 | Depth & Stereo — StereoDepth node, heatmap, depth math | ✅ Complete |
| 4 | Spatial Data — click pixel → XYZ in metres | ✅ Complete |
| 5 | Depth Stability — on-device filtering and host-side smoothing | ✅ Complete (CUDA deferred) |
| 6 | Neural Inference — .blob model on OAK VPU, detection overlays | ⬜ Planned |

---

## Stage 2 — What It Does

- `CameraStream` class builds a depthai pipeline (ColorCamera → XLinkOut), opens the device, and owns the GL texture
- `StreamView` UI panel renders the live frame via `ImGui::Image()`, scaled to fill available width
- Sidebar shows "Streaming" in green when the pipeline is active
- App shows a "No stream" placeholder and retries connection when no device is connected

![Stage 2 screenshot — Camera window showing live RGB frame, sidebar showing Streaming](stage2.png)

---

## Stage 4 — What It Does

- `SpatialLocationCalculator` node wired to `StereoDepth` output — uses the median depth in a 5%×5% ROI and returns real-world XYZ coordinates using the camera's built-in intrinsics
- `XLinkIn` ("spatialConfig") receives `SpatialLocationsConfig` packets from host when the ROI changes — the reverse of the normal device→host data flow
- `StreamView` detects mouse clicks (Lock mode) or hover position (Track mode) and calls `streams.setRoi()` with normalised coordinates
- Corner-bracket reticle drawn via `ImGui::GetWindowDrawList()` marks the current sample point
- Sidebar shows colour-coded XYZ readout (X=blue, Y=green, Z=orange) with a Lock/Track radio toggle

![Stage 4 screenshot — Camera window with reticle, Sidebar showing XYZ readout](stage4.png)

---

## Stage 5 — What It Does

- StereoDepth applies left-right, median, speckle, and temporal filtering on the device
- Host-side temporal smoothing and invalid-pixel filling stabilize the displayed depth heatmap
- CUDA acceleration remains deferred; this stage uses the OAK device and CPU instead

---

## Stage 3 — What It Does

- `MonoCamera` left/right nodes feed a `StereoDepth` node configured with the `HIGH_DENSITY` preset
- Depth output is uint16 mm values via `stereo->depth` (not disparity), aligned to the RGB camera; dimensions are taken from the incoming frame
- Turbo colormap LUT (256 entries) applied on CPU each frame: close pixels → red, far → purple, invalid (0) → black
- `DepthView` panel renders the heatmap texture and an ImPlot histogram of valid depth values
- Raw uint16 depth buffer exposed via `rawDepthData()` for the histogram; pointer valid only until next `poll()`

![Stage 3 screenshot — Depth window showing Turbo heatmap and histogram](stage3.png)

---

## Stage 1 — What It Does

- GLFW window with OpenGL 3.3 core profile context
- ImGui docking layout (requires ImGui docking branch — see below)
- Fixed sidebar showing:
  - Green/red connection indicator
  - Device MX ID
  - Boot state: "Unbooted (ready)" or "Booted / in use"
- `OakDevice` wrapper that polls `dai::XLinkConnection::getAllConnectedDevices()` once per second (throttled — USB enumeration costs ~200ms)

![Stage 1 screenshot — sidebar showing Connected, MX ID, and Unbooted state](stage1.png)

---

## Reference Documentation

Standalone HTML files — open directly in a browser, no server needed.

| File | Contents |
|---|---|
| [`docs/reference/oak-reference.html`](docs/reference/oak-reference.html) | Timeless concepts: pipeline mental model, node types, data flow, 6-stage overview |
| [`docs/reference/oak-stage1.html`](docs/reference/oak-stage1.html) | Stage 1 deep-dive: what we built, annotated code, all gotchas encountered |
| [`docs/reference/oak-stage2.html`](docs/reference/oak-stage2.html) | Stage 2 deep-dive: pipeline boot, ColorCamera node, GL texture lifecycle, gotchas |

---

## Dependencies

| Dependency | Version | Notes |
|---|---|---|
| [depthai-core](https://github.com/luxonis/depthai-core) | 2.32.0 | Prebuilt Windows binaries required. v2.17.3 and earlier lack `libusb-1.0.dll` on Windows. |
| [GLFW](https://www.glfw.org/) | 3.4 | Prebuilt Win64 binaries |
| [Dear ImGui](https://github.com/ocornut/imgui) | docking branch | **Must be the `docking` branch** — master/release zips do not include `DockSpaceOverViewport` |
| CMake | 3.20+ | |
| Visual Studio 2022 / 2026 | | C++17, MSVC |

---

## Building on Windows

### 1. Get the prebuilt dependencies

**depthai-core v2.32.0:**
Download the Windows prebuilt from the [depthai-core releases](https://github.com/luxonis/depthai-core/releases) page and extract to a known path (e.g. `E:\luxonis\depthai-win64\`).

**GLFW 3.4:**
Download the 64-bit Windows binaries from [glfw.org/download](https://www.glfw.org/download.html) and extract to a known path (e.g. `E:\luxonis\glfw-win64\`).

### 2. Clone this repo

```bash
git clone https://github.com/AlfredBr/oak-explorer.git
cd oak-explorer
```

### 3. Get ImGui docking branch

```bash
cd third_party
curl -L "https://github.com/ocornut/imgui/archive/refs/heads/docking.zip" -o imgui-docking.zip
# Extract and rename the folder to: third_party/imgui/
```

### 4. Edit CMakePresets.json

Update the paths to match your dependency locations:

```json
"cacheVariables": {
    "depthai_DIR": "C:/path/to/depthai-win64/lib/cmake/depthai",
    "DEPTHAI_WIN64_DIR": "C:/path/to/depthai-win64",
    "GLFW_ROOT": "C:/path/to/glfw-win64"
}
```

### 5. Open in Visual Studio and build

**File → Open → Folder** (not "Open Solution") — point it at the `oak-explorer` folder.

VS reads `CMakePresets.json` automatically. Select `windows-debug` from the configuration dropdown and build (Ctrl+Shift+B).

> The POST_BUILD step copies depthai's DLLs (including `libusb-1.0.dll`) next to the executable automatically.

#### VS2026 Insiders — command-line build workaround

VS2026 Insiders has a bug where CMake re-runs triggered by MSBuild lose the preset's `cacheVariables` (depthai_DIR is not passed). Use the terminal instead:

```bash
cmake --preset windows-release
cmake --build out/build/release --config Release
# exe: out\build\release\Release\oak-explorer.exe
```

Debug builds work normally through the VS UI.

---

## Project Structure

```
oak-explorer/
├── src/
│   ├── main.cpp              # GLFW window, OpenGL context, ImGui render loop
│   ├── oak/
│   │   ├── Device.h          # OakDevice interface (USB enumeration, throttled)
│   │   ├── Device.cpp        # depthai device enumeration wrapper
│   │   └── OakStreams.h/.cpp # RGB + StereoDepth + SpatialLocationCalculator pipeline
│   └── ui/
│       ├── Sidebar.h/.cpp    # sidebar: streaming state, spatial XYZ, mode toggle
│       ├── StreamView.h/.cpp # Camera window: live RGB, click/hover ROI, reticle overlay
│       ├── DepthView.h/.cpp  # Depth window: Turbo heatmap + ImPlot histogram
│       └── SpatialMode.h     # SpatialMode enum (Lock / Track)
├── third_party/
│   └── imgui/                # ImGui docking branch (vendored)
├── CMakeLists.txt
└── CMakePresets.json
```

---

## Key Lessons (Stage 4)

- `SpatialLocationCalculator` takes `stereo->depth` as input (not disparity) — miswiring to disparity gives nonsense XYZ
- `setWaitForConfigInput(false)` is required — without it the node stalls waiting for a host config packet before producing any output
- `XLinkIn` sends `SpatialLocationCalculatorConfig` objects (not raw bytes) via `DataInputQueue::send(cfg)`
- ROI expressed as `dai::Rect(dai::Point2f(x0,y0), dai::Point2f(x1,y1))` in normalised [0,1] coords; clamp to keep the 5% half-width inside the frame
- `spatialCoordinates.x/y/z` are in mm — divide by 1000 for metres
- Coordinate system: X right, Y down, Z forward (away from camera); positive Z is always the depth distance
- `MEDIAN` algorithm is more robust than `AVERAGE` for stereo noise — avoids outlier depth values skewing the result
- `tryGet()` on the spatial queue returns nullptr when no new data — safe to call every frame, just skip

---

## Key Lessons (Stage 3)

- `stereo->depth` output is uint16 mm (zero = invalid); `stereo->disparity` is a different stream with different semantics — don't mix them
- `MonoCamera` board sockets on OAK-D-Lite: `CAM_B` = left, `CAM_C` = right (OAK-D uses `LEFT`/`RIGHT`; OAK-D-Lite uses `CAM_B`/`CAM_C`)
- `StereoDepth::PresetMode::HIGH_DENSITY` trades range for fill rate — good default for indoor use
- Turbo colormap must be inverted to feel natural: close = warm (red), far = cool (purple); map `d` to `(10000−d)/(10000−400)×255`
- `ImPlot::CreateContext()` must be called after `ImGui::CreateContext()`; `ImPlot::DestroyContext()` before `ImGui::DestroyContext()`
- `ImPlot::BeginPlot` with size `ImVec2(-1, height)` fills available width — consistent with how `ImGui::Image` is scaled
- `rawDepthData()` pointer is invalidated on the next `poll()` — copy to a local vector before the next frame if you need to keep it

---

## Key Lessons (Stage 2)

- `setInterleaved(true)` is required for `GL_RGB` — `false` outputs planar CHW (R plane, G plane, B plane) which renders as a 3×3 tiled grayscale grid
- `GL_BGR` is not valid in OpenGL 3.3 core profile — use `ColorOrder::RGB` + `GL_RGB`
- `#include <GLFW/glfw3.h>` not `<GL/GL.h>` — Windows SDK GL.h only covers GL 1.1; mixing it with depthai headers causes WINGDIAPI/APIENTRY redefinition errors
- `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)` before texture creation — RGB rows at 1280px wide are not 4-byte aligned; restore to 4 after
- `glTexImage2D` once at startup (allocates VRAM), `glTexSubImage2D` every frame (updates in place, no reallocation)
- `queue_.reset()` before `device_.reset()` in destructor — release order matters
- `stream.close()` before ImGui/GLFW teardown — GL resources must be freed while the context is still active
- `OakDevice::poll()` must be throttled — USB enumeration costs ~200ms per call; running it every frame kills frame rate
- `imgui.ini` saves docking layout — delete it to reset if windows dock into the wrong panel

---

## Key Lessons (Stage 1)

- `getAllConnectedDevices()` is on `dai::XLinkConnection`, not `dai::DeviceBase`
- ImGui's default font (ProggyClean) is ASCII-only — use `ImGui::Bullet()` not `●`
- ImGui docking requires the `docking` branch — standard releases don't include it
- depthai v2.32.0+ bundles `libusb-1.0.dll`; older versions do not (silent failure on Windows)
- A device in `X_LINK_UNBOOTED` state is healthy and ready — it just hasn't had a pipeline uploaded yet

---

## License

MIT
