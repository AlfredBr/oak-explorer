# oak-explorer

A Windows desktop application and C++ learning project for the [Luxonis OAK-D-Lite](https://shop.luxonis.com/products/oak-d-lite) depth camera. It uses DepthAI, Dear ImGui, ImPlot, GLFW, and OpenGL to display camera streams and sample 3D coordinates.

Stages 1–5 are complete, including an optional CUDA backend for host-side depth processing. Stage 6 includes object detection on either the OAK VPU or the host GPU, plus a depth-based camera and detection limit. Spatial XYZ detections are still planned.

## Features and stages

| Stage | Feature | Status |
|---|---|---|
| 1 | Window, dockable ImGui layout, and device status | Complete |
| 2 | Live RGB camera stream | Complete |
| 3 | Stereo depth heatmap and depth histogram | Complete |
| 4 | Interactive spatial XYZ sampling | Complete |
| 5 | Depth filtering and flicker reduction | Complete; optional CUDA path |
| 6 | Neural inference and detection overlays | Partial (detections and overlay done; spatial detections planned) |

### Stage 1 — Window and device status

- Creates a GLFW window with an OpenGL 3.3 core context and a dockable ImGui interface.
- Shows connection state, device MX ID, and whether the device is ready or in use.
- Persists the window geometry and ImGui layout beside the executable.

![Oak Explorer device status sidebar](stage1.png)

### Stage 2 — RGB camera stream

- `OakStreams` builds the DepthAI pipeline, owns the device and GL textures, and polls the RGB, depth, and spatial queues.
- The Camera window displays the live 1920×1080 RGB stream.
- If the camera is missing or the stream fails, the app keeps running and retries the pipeline every two seconds.

![Oak Explorer live camera stream](stage2.png)

### Stage 3 — Stereo depth

- Left and right `MonoCamera` nodes feed a `StereoDepth` node using the `HIGH_DENSITY` preset.
- Depth is a uint16 millimetre value, aligned to the RGB camera. The output dimensions are taken from each frame.
- The Depth window shows a Turbo-coloured heatmap. Depth Distribution plots valid raw depth values; zero represents invalid depth.

![Depth Distribution histogram](depth-histogram.png)

### Stage 4 — Spatial sampling

- `SpatialLocationCalculator` measures a 5%×5% region of interest (ROI) and reports X, Y, and Z in metres.
- **Lock** mode samples where you click in the Camera window; **Track** mode follows the pointer while it is over the image.
- A reticle marks the current ROI, and the sidebar displays the coordinate readout.

### Stage 5 — Depth stability

- Enables left-right consistency, median, speckle, and temporal filtering in the device's `StereoDepth` pipeline.
- Applies host-side temporal smoothing and fills invalid display pixels from their 3×3 neighbourhood.
- Optional CUDA path (`OAK_ENABLE_CUDA`): the same EMA, hole fill, and Turbo colormap run on an NVIDIA GPU. The CPU path remains the default and the automatic fallback. See [Optional: CUDA depth processing](#optional-cuda-depth-processing).

### Stage 6 — Neural inference (partial)

Choose the detection backend in the sidebar:

- **OAK VPU** runs the bundled YOLOv6n COCO model (`models/yolov6n_coco_416x416_openvino_2022.1_6shave.blob`, 416×416, 80 classes) on the camera. It uses the RGB preview and maps letterboxed detections back onto the full Camera view.
- **Host GPU** runs the bundled YOLOv8s COCO model (`models/yolov8s.onnx`, 640×640) through ONNX Runtime's DirectML provider on the PC GPU. It processes the full 1920×1080 camera frame. Model initialization runs in the background; inference runs on a worker thread. The sidebar displays the inference rate and time.

Both backends share the detection overlay and minimum-confidence slider. The model files are copied beside the executable during the build. Host GPU inference is optional: set `OAK_ONNXRUNTIME_DIR` to an extracted `Microsoft.ML.OnnxRuntime.DirectML` NuGet package containing `build/native/include` and `runtimes/win-x64/native`, and place `DirectML.dll` from `Microsoft.AI.DirectML` in that native folder. If ONNX Runtime is unavailable at configure time, the app builds without the host-GPU backend. DirectML requires a compatible DirectX 12 GPU; it does not require CUDA or cuDNN.

Spatial XYZ detections are not implemented yet.

### Depth squelch

The sidebar's **Depth squelch** section has a **Hide far pixels** checkbox and a distance slider (0.3–10 m). When enabled, pixels farther than the selected distance are blacked out in the Camera view using the depth map aligned to RGB. **Hide unknown** also blacks out pixels without a valid depth reading.

**Limit detection** is enabled by default and applies the same distance limit to detection. The Host GPU model receives the squelched image, so it cannot see pixels beyond the limit. The OAK VPU processes camera frames internally and cannot be masked, so detections are instead discarded when the median valid depth in the middle of a box is beyond the limit; boxes with no valid depth samples are kept. Turn off **Limit detection** to continue detecting objects beyond the visual squelch.

## Controls and windows

- Choose **Lock** or **Track** in the sidebar to control spatial sampling.
- Click or hover over the Camera image according to the selected mode.
- In **Detection**, switch between **OAK VPU** and **Host GPU**, toggle boxes, and adjust the minimum confidence.
- In **Depth squelch**, enable far-pixel masking, set the maximum distance, optionally hide pixels with unknown depth, and choose whether the same limit applies to detection.
- The **Depth** window displays the heatmap; **Depth Distribution** shows the raw-depth histogram.
- **Stereo Cameras** shows synchronized left/right grayscale images. Choose **Separate** for side-by-side images or **Overlay** with an adjustable right-image blend (0 = left only, 1 = right only). These are raw camera views, so lens distortion and stereo parallax remain visible.
- Move and dock the windows using ImGui. The layout is saved in `imgui.ini`; window geometry is saved in `window.ini`, both beside the executable.
- Without an available camera, the stream panels show a placeholder while the app retries.

## Project structure

```text
oak-explorer/
├── src/
│   ├── main.cpp              # GLFW/OpenGL setup, render loop, reconnect attempts
│   ├── oak/
│   │   ├── Device.h/.cpp      # OAK device enumeration and status
│   │   └── OakStreams.h/.cpp  # RGB, stereo depth, and spatial pipeline
│   ├── detect/
│   │   ├── HostDetector.h/.cpp  # YOLOv8 on the host GPU (ONNX Runtime + DirectML)
│   │   └── HostDetectorStub.cpp # Fallback when ONNX Runtime is unavailable
│   ├── cuda/
│   │   └── DepthProcessor.h/.cu # Optional CUDA depth post-processing
│   └── ui/
│       ├── Sidebar.h/.cpp       # Device status, detection and squelch controls
│       ├── StreamView.h/.cpp    # RGB view, detections, ROI input, and reticle
│       ├── DepthView.h/.cpp     # Depth heatmap and histogram
│       ├── DetectionSettings.h  # Detection and depth-squelch UI state
│       └── SpatialMode.h        # Lock/Track mode
├── models/                      # Bundled YOLOv6n blob and YOLOv8s ONNX model
├── third_party/
│   ├── imgui/                 # Dear ImGui docking branch
│   └── implot/                # ImPlot
├── docs/reference/            # Standalone OAK and stage reference pages
├── CMakeLists.txt
└── CMakePresets.json
```

## Requirements

| Dependency | Version / details |
|---|---|
| Windows and Visual Studio | The checked-in presets use the Visual Studio 18 2026 x64 generator and MSVC |
| CMake | 3.20 or newer, with support for the selected Visual Studio generator |
| DepthAI Core | 2.32.0 Windows prebuilt package |
| GLFW | 3.4 Win64 prebuilt package; the library is expected under `lib-vc2022` |
| Dear ImGui | Docking branch, vendored in `third_party/imgui` |
| ImPlot | Vendored in `third_party/implot` |
| OpenGL | 3.3 core support from the graphics driver |

A CUDA toolkit and NVIDIA GPU are only needed for the optional CUDA depth path. Host-GPU detection is optional and uses ONNX Runtime with DirectML, which requires a compatible DirectX 12 GPU; it does not require CUDA or cuDNN. The OAK VPU detector does not require host-GPU inference dependencies.

## Build on Windows

### 1. Get the repository and binary dependencies

```powershell
git clone https://github.com/AlfredBr/oak-explorer.git
cd oak-explorer
```

Download the Windows prebuilt packages for [DepthAI Core](https://github.com/luxonis/depthai-core/releases) 2.32.0 and [GLFW](https://www.glfw.org/download.html) 3.4. Extract them to directories of your choice. Dear ImGui and ImPlot are already included in the repository. The OAK VPU model and ONNX model are included in `models/`. To build with host-GPU detection, also obtain the ONNX Runtime DirectML package and `DirectML.dll` from `Microsoft.AI.DirectML`; this is optional if you only need the OAK VPU detector.

### 2. Set local dependency paths

Edit `depthai_DIR`, `DEPTHAI_WIN64_DIR`, and `GLFW_ROOT` in both configure presets in `CMakePresets.json`. For example:

```json
"depthai_DIR": "C:/path/to/depthai-win64/lib/cmake/depthai",
"DEPTHAI_WIN64_DIR": "C:/path/to/depthai-win64",
"GLFW_ROOT": "C:/path/to/glfw-win64"
```

`GLFW_ROOT` should contain `include` and `lib-vc2022` directories. `DEPTHAI_WIN64_DIR` should contain the package's `bin` directory; CMake copies those runtime DLLs beside the built executable. For host-GPU detection, set the CMake cache path `OAK_ONNXRUNTIME_DIR` to the extracted ONNX Runtime DirectML package. The build copies its runtime DLLs beside the executable when found; otherwise the VPU backend remains available.

### 3. Configure and build

Run these commands from a Visual Studio Developer PowerShell or another terminal where CMake and MSVC are available:

```powershell
# Release
cmake --preset windows-release
cmake --build out/build/release --config Release

# Debug (optional)
cmake --preset windows-debug
cmake --build out/build/debug --config Debug
```

The Release executable is `out\build\release\Release\oak-explorer.exe`. Alternatively, open the repository root in Visual Studio using **File → Open → Folder** and select a configure preset.

## Optional: CUDA depth processing

The host-side depth stage (temporal EMA, 3×3 hole fill, Turbo colormap) can run on an NVIDIA GPU. It is off by default.

```powershell
cmake --preset windows-release-cuda
cmake --build --preset windows-release-cuda --config Release
```

- Requires the CUDA Toolkit (tested with 13.1) and an NVIDIA GPU. The preset targets compute capability 8.6 (RTX 30xx); change `OAK_CUDA_ARCH` for other GPUs.
- `src/cuda/DepthProcessor.cu` is compiled by calling `nvcc` directly, so the CUDA Visual Studio integration is not needed.
- CUDA 13.x rejects the newest MSVC. `OAK_CUDA_MSVC_TOOLSET` (preset default `14.44`) selects an older installed toolset for that one file; clear it if your MSVC is supported.
- At startup the app logs `depth post-processing on CUDA` or `on CPU`, and every 120 frames logs the average processing time. If CUDA is unavailable or fails, it falls back to the CPU path.
- Results match the CPU path except for hole filling, which fills one 3×3 ring per frame instead of cascading through a frame.

On the development machine (RTX 3080, 1920×1080 depth) the CPU path averages about 5.3 ms/frame. The CUDA path takes about 1 ms while the GPU is at boost clocks, but this workload is too light to keep the GPU out of its idle power state, where it rises to roughly 5 ms. Measure on your own system before relying on it.

## Run and troubleshoot

Connect an OAK-D-Lite over USB before launching for live streams. The application remains open if the camera is absent and retries every two seconds. If the device is reported as in use, close the other application that owns it and wait for the next retry.

Delete `imgui.ini` beside the executable to reset the dock layout. Delete `window.ini` to restore the default main-window size and position.

## Reference documentation

These standalone HTML pages can be opened directly in a browser:

- [OAK reference](docs/reference/oak-reference.html) — pipeline concepts, node types, and data flow.
- [Stage 1 deep dive](docs/reference/oak-stage1.html) — window, device detection, and implementation notes.
- [Stage 2 deep dive](docs/reference/oak-stage2.html) — camera pipeline, texture lifecycle, and implementation notes.

## License

MIT
