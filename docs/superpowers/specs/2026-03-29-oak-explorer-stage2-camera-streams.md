# Stage 2 — Camera Streams Design Spec

## Goal

Open a `ColorCamera` pipeline on the OAK-D-Lite, stream live RGB frames over USB, upload each frame as an OpenGL texture, and display it in a dockable ImGui window — live video inside the harness.

## Architecture

Two new classes follow the same separation established in Stage 1:

- **`oak::CameraStream`** owns all depthai and OpenGL concerns: builds the pipeline, opens `dai::Device`, manages the output queue, creates and owns the GL texture, uploads frames each poll.
- **`ui::StreamView`** owns the ImGui rendering: opens a "Camera" window, calls `ImGui::Image()` with the texture ID from CameraStream, scales to fill available width at correct aspect ratio.

They communicate through a read-only interface (`textureId()`, `width()`, `height()`, `isStreaming()`). The UI layer never touches depthai. The stream layer never touches ImGui. This boundary holds through all six stages.

## Tech Stack

C++17, depthai-core **v2.32.0** (upgraded from v2.17.3 during Stage 1 — needed for bundled `libusb-1.0.dll` on Windows; the original design spec lists v2.17.3 but that is outdated), ImGui (docking branch), OpenGL 3.3, GLFW 3.4, CMake + VS2022

---

## File Map

| File | Status | Responsibility |
|---|---|---|
| `src/oak/CameraStream.h` | **New** | Interface: open/close, poll, textureId, width, height, isStreaming |
| `src/oak/CameraStream.cpp` | **New** | Pipeline build, dai::Device, output queue, GL texture create + upload |
| `src/ui/StreamView.h` | **New** | Interface: renderStreamView(const CameraStream&) |
| `src/ui/StreamView.cpp` | **New** | ImGui "Camera" window with ImGui::Image(), aspect-ratio scaling |
| `src/main.cpp` | **Modified** | Construct CameraStream, add stream.poll() and renderStreamView() to loop |
| `src/ui/Sidebar.cpp` | **Modified** | Add streaming state display — pass CameraStream to renderSidebar so it can show "Streaming" vs "Booted / in use" |
| `CMakeLists.txt` | **Modified** | Add CameraStream.cpp and StreamView.cpp to sources |

---

## CameraStream Interface

```cpp
// Required includes in CameraStream.cpp:
//   #include <depthai/depthai.hpp>         — full depthai SDK
//   #include <GL/GL.h>                     — GLuint (Windows system OpenGL)
// Required includes in CameraStream.h:
//   #include <cstdint>
//   Forward-declare GLuint as: typedef unsigned int GLuint;
//   (avoids pulling all of GL into every translation unit that includes the header)

namespace oak {

class CameraStream {
public:
    // Open the pipeline — builds dai::Pipeline, opens dai::Device,
    // creates GL texture. Must be called after OpenGL context is active.
    // Does not throw — logs to stderr on failure, leaves isStreaming() = false.
    void open();

    // Close and release the device. Safe to call if not open.
    // Destruction order: release queue_ before device_ to avoid
    // accessing a stopped device's queue during shutdown.
    void close();

    // Poll for a new frame. Upload to GL texture if one is available.
    // Never throws. Call once per render frame.
    void poll();

    bool isStreaming() const;
    GLuint textureId() const;
    int width() const;
    int height() const;

private:
    std::unique_ptr<dai::Device> device_;
    std::shared_ptr<dai::DataOutputQueue> queue_;
    GLuint texId_ = 0;
    int width_ = 1280;
    int height_ = 720;
    bool streaming_ = false;
};

} // namespace oak
```

### Pipeline configuration

```cpp
dai::Pipeline pipeline;

auto cam = pipeline.create<dai::node::ColorCamera>();
cam->setPreviewSize(1280, 720);
cam->setInterleaved(false);
cam->setColorOrder(dai::ColorCameraProperties::ColorOrder::BGR);
cam->setFps(30);

auto xout = pipeline.create<dai::node::XLinkOut>();
xout->setStreamName("rgb");
cam->preview.link(xout->input);
```

### GL texture creation (once, inside open())

```cpp
glGenTextures(1, &texId_);
glBindTexture(GL_TEXTURE_2D, texId_);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
    width_, height_, 0, GL_BGR, GL_UNSIGNED_BYTE, nullptr);
glBindTexture(GL_TEXTURE_2D, 0); // unbind — avoid state leak
```

> Note: `GL_BGR` is supported on all modern drivers including NVIDIA RTX. Resolution 1280×720 is chosen to stay well within the OAK-D-Lite preview port bandwidth at 30fps.

### Frame upload (each poll(), if frame available)

```cpp
auto frame = queue_->tryGet<dai::ImgFrame>();
if (frame) {
    glBindTexture(GL_TEXTURE_2D, texId_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
        width_, height_, GL_BGR, GL_UNSIGNED_BYTE,
        frame->getData().data());
    glBindTexture(GL_TEXTURE_2D, 0); // unbind — avoid state leak
}
```

---

## StreamView Interface

```cpp
namespace ui {
    void renderStreamView(const oak::CameraStream& stream);
} // namespace ui
```

### Rendering

```cpp
ImGui::Begin("Camera");
if (stream.isStreaming()) {
    float avail = ImGui::GetContentRegionAvail().x;
    float aspect = (float)stream.height() / (float)stream.width();
    ImGui::Image(
        (ImTextureID)(uintptr_t)stream.textureId(),
        ImVec2(avail, avail * aspect)
    );
} else {
    ImGui::TextDisabled("No stream");
}
ImGui::End();
```

---

## main.cpp changes

```cpp
// Construct after OpenGL context is active
oak::CameraStream stream;
stream.open();

// In render loop — after glfwPollEvents(), before ImGui::NewFrame()
device.poll();       // existing — updates connection status
stream.poll();       // new — uploads latest frame to GL texture if available

// In render loop — inside ImGui frame (after ImGui::NewFrame())
ui::renderSidebar(device, stream);   // pass stream so sidebar can show streaming state
ui::renderStreamView(stream);        // new Camera window
```

### Sidebar update

`renderSidebar` gains a second parameter `const oak::CameraStream& stream`. When `stream.isStreaming()` is true, replace the "Unbooted (ready)" / "Booted / in use" state label with **"Streaming"** in green.

---

## Error Handling

- `CameraStream::open()` wraps `dai::Device` construction in try/catch. On failure, logs to stderr and leaves `streaming_ = false`. Does not throw.
- `CameraStream::poll()` does not throw. If `tryGet()` throws (shouldn't, but defensively), catches and marks streaming false.
- `CameraStream::close()` is safe to call at any time, including if `open()` failed.
- If the device is unplugged mid-session, `tryGet()` will start returning nullptr. Streaming silently stops showing new frames. A future stage will add reconnect logic.

---

## Out of Scope (Stage 3+)

- Exposure / white balance / ISP controls
- Depth stream
- Multiple simultaneous streams
- Recording / saving frames
- CUDA processing

---

## Done Criteria

- [ ] App builds and launches without error
- [ ] Live RGB video visible in the "Camera" ImGui window
- [ ] Sidebar shows "Streaming" in green while streaming
- [ ] Aspect ratio is correct (no stretch)
- [ ] App does not crash on launch when device is unplugged
- [ ] Tagged `stage-2-complete`
