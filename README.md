# oak-explorer

A C++ learning harness for the [Luxonis OAK-D-Lite](https://shop.luxonis.com/products/oak-d-lite) depth camera.

Built with ImGui + OpenGL3 + GLFW. The goal is to build up a single growing application across six stages, understanding every line rather than copying demo code.

---

## Stages

| Stage | Name | Status |
|---|---|---|
| 1 | Foundation — ImGui window, device detection sidebar | ✅ Complete |
| 2 | Camera Streams — live RGB frame as ImGui texture | ⬜ Planned |
| 3 | Depth & Stereo — StereoDepth node, heatmap, depth math | ⬜ Planned |
| 4 | Spatial Data — click pixel → XYZ in metres | ⬜ Planned |
| 5 | CUDA Acceleration — RTX GPU kernels, OpenGL PBO interop | ⬜ Planned |
| 6 | Neural Inference — .blob model on OAK VPU, detection overlays | ⬜ Planned |

---

## Stage 1 — What It Does

- GLFW window with OpenGL 3.3 core profile context
- ImGui docking layout (requires ImGui docking branch — see below)
- Fixed sidebar showing:
  - Green/red connection indicator
  - Device MX ID
  - Boot state: "Unbooted (ready)" or "Booted / in use"
- `OakDevice` wrapper that polls `dai::XLinkConnection::getAllConnectedDevices()` once per frame without throwing

![Stage 1 screenshot — sidebar showing Connected, MX ID, and Unbooted state](/stage1.png)

---

## Dependencies

| Dependency | Version | Notes |
|---|---|---|
| [depthai-core](https://github.com/luxonis/depthai-core) | 2.32.0 | Prebuilt Windows binaries required. v2.17.3 and earlier lack `libusb-1.0.dll` on Windows. |
| [GLFW](https://www.glfw.org/) | 3.4 | Prebuilt Win64 binaries |
| [Dear ImGui](https://github.com/ocornut/imgui) | docking branch | **Must be the `docking` branch** — master/release zips do not include `DockSpaceOverViewport` |
| CMake | 3.20+ | |
| Visual Studio 2022 | | C++17, MSVC |

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

### 5. Open in Visual Studio 2022

**File → Open → Folder** (not "Open Solution") — point it at the `oak-explorer` folder.

VS2022 reads `CMakePresets.json` automatically. Select `windows-debug` from the configuration dropdown and build (Ctrl+Shift+B).

> The POST_BUILD step copies depthai's DLLs (including `libusb-1.0.dll`) next to the executable automatically.

---

## Project Structure

```
oak-explorer/
├── src/
│   ├── main.cpp          # GLFW window, OpenGL context, ImGui render loop
│   ├── oak/
│   │   ├── Device.h      # OakDevice interface
│   │   └── Device.cpp    # depthai device enumeration wrapper
│   └── ui/
│       ├── Sidebar.h     # renderSidebar() declaration
│       └── Sidebar.cpp   # ImGui sidebar panel
├── third_party/
│   └── imgui/            # ImGui docking branch (vendored)
├── CMakeLists.txt
└── CMakePresets.json
```

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
