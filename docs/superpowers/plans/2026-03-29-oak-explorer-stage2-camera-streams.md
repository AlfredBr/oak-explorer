# Stage 2 — Camera Streams Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Open a ColorCamera pipeline on the OAK-D-Lite, stream live RGB frames over USB, upload each frame as an OpenGL texture, and display it in a dockable ImGui window.

**Architecture:** `oak::CameraStream` owns all depthai and OpenGL concerns (pipeline, device, queue, GL texture). `ui::StreamView` owns the ImGui Camera window. They communicate through a read-only interface. The UI layer never touches depthai; the stream layer never touches ImGui.

**Tech Stack:** C++17, depthai-core v2.32.0, ImGui docking branch, OpenGL 3.3, GLFW 3.4, CMake + VS2022

---

## Context for implementers

> **Known spec deviation — Task 3:** The design spec's pipeline and GL texture blocks use `ColorOrder::BGR` / `GL_BGR`. That is wrong: `GL_BGR` is not a valid format enum in an OpenGL 3.3 core profile context — it produces `GL_INVALID_ENUM` and a black texture at runtime. This plan intentionally overrides those spec lines, using `ColorOrder::RGB` and `GL_RGB` throughout, which is correct. The spec note that says "GL_BGR is supported on all modern drivers" applies only to compatibility profiles, not core.

- Working directory: `E:\luxonis\oak-explorer\`
- Build: VS2022, File → Open → Folder, preset `windows-debug`, Ctrl+Shift+B
- Run: F5 or Ctrl+F5 from VS2022; executable is in `out\build\windows-debug\`
- depthai-core v2.32.0 prebuilt: `E:\luxonis\depthai-win64\`
- Stage 1 is complete and tagged `stage-1-complete` — all Stage 1 files compile and run
- The device must be plugged in via USB3 for runtime testing

**No unit test framework is present.** Verification is: build succeeds (0 errors) + visual/runtime confirmation. Each task ends with a build check followed by a commit.

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/oak/CameraStream.h` | Create | Interface: open/close/poll/textureId/width/height/isStreaming |
| `src/oak/CameraStream.cpp` | Create | Pipeline, dai::Device, output queue, GL texture, frame upload |
| `src/ui/StreamView.h` | Create | Interface: renderStreamView(const CameraStream&) |
| `src/ui/StreamView.cpp` | Create | ImGui "Camera" window with ImGui::Image(), aspect scaling |
| `src/ui/Sidebar.h` | Modify | Add CameraStream parameter to renderSidebar |
| `src/ui/Sidebar.cpp` | Modify | Show "Streaming" in green when stream.isStreaming() |
| `src/main.cpp` | Modify | Construct CameraStream, add stream.poll() + renderStreamView() |
| `CMakeLists.txt` | Modify | Add CameraStream.cpp and StreamView.cpp to sources |

---

## Task 1: Add new sources to CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1.1: Add CameraStream.cpp and StreamView.cpp to the executable sources**

Open `CMakeLists.txt`. Find the `add_executable` block (lines 29–34). Add the two new sources:

```cmake
add_executable(oak-explorer
    src/main.cpp
    src/oak/Device.cpp
    src/oak/CameraStream.cpp
    src/ui/Sidebar.cpp
    src/ui/StreamView.cpp
    ${IMGUI_SOURCES}
)
```

- [ ] **Step 1.2: Verify the change looks right**

The file should now list 5 `.cpp` sources before `${IMGUI_SOURCES}`. No other changes.

- [ ] **Step 1.3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add CameraStream and StreamView sources to CMakeLists"
```

---

## Task 2: Write CameraStream.h

**Files:**
- Create: `src/oak/CameraStream.h`

- [ ] **Step 2.1: Create the header**

Create `src/oak/CameraStream.h` with this exact content:

```cpp
// src/oak/CameraStream.h
#pragma once
#include <memory>
#include <cstdint>

// Forward-declare GLuint to avoid pulling OpenGL headers into every
// translation unit that includes this header.
typedef unsigned int GLuint;

// Forward-declare depthai types used in private members
namespace dai {
    class Device;
    class DataOutputQueue;
}

namespace oak {

class CameraStream {
public:
    CameraStream();
    ~CameraStream();

    // Open the pipeline — builds dai::Pipeline, opens dai::Device,
    // creates GL texture. Must be called after the OpenGL context is active.
    // Does not throw. Logs to stderr on failure; isStreaming() stays false.
    void open();

    // Close and release the device.
    // Safe to call at any time, including if open() was never called or failed.
    void close();

    // Poll for a new frame and upload to GL texture if one is available.
    // Never throws. Call once per render frame before ImGui::NewFrame().
    void poll();

    bool   isStreaming() const { return streaming_; }
    GLuint textureId()   const { return texId_; }
    int    width()       const { return width_; }
    int    height()      const { return height_; }

private:
    std::unique_ptr<dai::Device>           device_;
    std::shared_ptr<dai::DataOutputQueue>  queue_;
    GLuint texId_     = 0;
    int    width_     = 1280;
    int    height_    = 720;
    bool   streaming_ = false;
};

} // namespace oak
```

- [ ] **Step 2.2: Build to verify the header compiles**

In VS2022: Ctrl+Shift+B
Expected: Build fails with a compiler error — "cannot open source file CameraStream.cpp" — that's fine, it means the header was found. If it says "cannot open include file CameraStream.h" something is wrong with the path.

> Note: Build will fail until Task 3 creates the `.cpp`. That is expected.

- [ ] **Step 2.3: Commit**

```bash
git add src/oak/CameraStream.h
git commit -m "feat: add CameraStream header"
```

---

## Task 3: Write CameraStream.cpp

**Files:**
- Create: `src/oak/CameraStream.cpp`

- [ ] **Step 3.1: Create the implementation file**

Create `src/oak/CameraStream.cpp` with this exact content:

```cpp
// src/oak/CameraStream.cpp
#include "oak/CameraStream.h"

#include <depthai/depthai.hpp>
#include <GL/GL.h>

#include <cstdio>

namespace oak {

CameraStream::CameraStream()  = default;
CameraStream::~CameraStream() { close(); }

void CameraStream::open() {
    try {
        // --- Build pipeline ---
        dai::Pipeline pipeline;

        auto cam = pipeline.create<dai::node::ColorCamera>();
        cam->setPreviewSize(width_, height_);  // 1280×720 — within OAK-D-Lite preview bandwidth
        cam->setInterleaved(false);
        cam->setColorOrder(dai::ColorCameraProperties::ColorOrder::RGB);  // GL_BGR is NOT valid in OpenGL 3.3 core profile — must use RGB
        cam->setFps(30);

        auto xout = pipeline.create<dai::node::XLinkOut>();
        xout->setStreamName("rgb");
        cam->preview.link(xout->input);

        // --- Open device (boots the pipeline onto the OAK VPU) ---
        device_ = std::make_unique<dai::Device>(pipeline);

        // --- Get output queue (non-blocking, max 4 frames buffered) ---
        queue_ = device_->getOutputQueue("rgb", 4, false);

        // --- Create GL texture ---
        // Set alignment to 1 — RGB rows are not 4-byte aligned at 1280px wide
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glGenTextures(1, &texId_);
        glBindTexture(GL_TEXTURE_2D, texId_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
            width_, height_, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        streaming_ = true;
        fprintf(stderr, "CameraStream: opened, streaming %dx%d @ 30fps\n",
            width_, height_);

    } catch (const std::exception& e) {
        fprintf(stderr, "CameraStream::open failed: %s\n", e.what());
        close();
    } catch (...) {
        fprintf(stderr, "CameraStream::open failed: unknown exception\n");
        close();
    }
}

void CameraStream::close() {
    streaming_ = false;
    queue_.reset();      // release queue before device
    device_.reset();     // destructor stops the pipeline
    if (texId_ != 0) {
        glDeleteTextures(1, &texId_);
        texId_ = 0;
    }
}

void CameraStream::poll() {
    if (!streaming_) return;
    try {
        auto frame = queue_->tryGet<dai::ImgFrame>();
        if (frame) {
            glBindTexture(GL_TEXTURE_2D, texId_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                width_, height_,
                GL_RGB, GL_UNSIGNED_BYTE,
                frame->getData().data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "CameraStream::poll error: %s\n", e.what());
        streaming_ = false;
    } catch (...) {
        streaming_ = false;
    }
}

} // namespace oak
```

- [ ] **Step 3.2: Build to verify CameraStream compiles cleanly**

In VS2022: Ctrl+Shift+B
Expected: Build fails only on linker errors about `StreamView` (not yet written). CameraStream itself should show 0 errors.
If there are errors in `CameraStream.cpp`, fix them before continuing.

- [ ] **Step 3.3: Commit**

```bash
git add src/oak/CameraStream.cpp
git commit -m "feat: implement CameraStream — pipeline, device, GL texture, poll"
```

---

## Task 4: Write StreamView.h and StreamView.cpp

**Files:**
- Create: `src/ui/StreamView.h`
- Create: `src/ui/StreamView.cpp`

- [ ] **Step 4.1: Create StreamView.h**

```cpp
// src/ui/StreamView.h
#pragma once
#include "oak/CameraStream.h"

namespace ui {

// Renders the Camera ImGui window.
// Call inside an active ImGui frame, after stream.poll().
void renderStreamView(const oak::CameraStream& stream);

} // namespace ui
```

- [ ] **Step 4.2: Create StreamView.cpp**

```cpp
// src/ui/StreamView.cpp
#include "ui/StreamView.h"
#include <imgui.h>

namespace ui {

void renderStreamView(const oak::CameraStream& stream) {
    ImGui::Begin("Camera");

    if (stream.isStreaming()) {
        // Scale image to fill available window width, preserve aspect ratio
        float avail  = ImGui::GetContentRegionAvail().x;
        float aspect = (float)stream.height() / (float)stream.width();
        ImGui::Image(
            (ImTextureID)(uintptr_t)stream.textureId(),
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

- [ ] **Step 4.3: Build to verify StreamView compiles**

In VS2022: Ctrl+Shift+B
Expected: Build may still fail with linker errors if `main.cpp` hasn't been updated yet. StreamView.cpp itself should show 0 errors.

- [ ] **Step 4.4: Commit**

```bash
git add src/ui/StreamView.h src/ui/StreamView.cpp
git commit -m "feat: implement StreamView — ImGui Camera window with aspect-ratio scaling"
```

---

## Task 5: Update Sidebar to show streaming state

**Files:**
- Modify: `src/ui/Sidebar.h`
- Modify: `src/ui/Sidebar.cpp`

- [ ] **Step 5.1: Update Sidebar.h — add CameraStream parameter**

Open `src/ui/Sidebar.h`. Replace the entire file with:

```cpp
// src/ui/Sidebar.h
#pragma once
#include "oak/Device.h"
#include "oak/CameraStream.h"

namespace ui {

// Renders the left sidebar. Call inside an active ImGui frame.
void renderSidebar(const oak::OakDevice& device, const oak::CameraStream& stream);

} // namespace ui
```

- [ ] **Step 5.2: Update Sidebar.cpp — show "Streaming" state**

Open `src/ui/Sidebar.cpp`. Make two changes:

**Change 1** — update the function signature (line 7):
```cpp
void renderSidebar(const oak::OakDevice& device, const oak::CameraStream& stream) {
```

**Change 2** — replace the State display block. Find this section:
```cpp
        ImGui::TextDisabled("State");
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
            info.available ? "Unbooted (ready)" : "Booted / in use");
        ImGui::Spacing();

        if (!info.available) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "(claimed by a process)");
        }
```

Replace with:
```cpp
        ImGui::TextDisabled("State");
        if (stream.isStreaming()) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Streaming");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                info.available ? "Unbooted (ready)" : "Booted / in use");
            if (!info.available) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "(claimed by a process)");
            }
        }
        ImGui::Spacing();
```

- [ ] **Step 5.3: Build to verify Sidebar compiles**

In VS2022: Ctrl+Shift+B
Expected: Build fails only because `main.cpp` still calls `renderSidebar(device)` with the old signature. That is expected and will be fixed in Task 6.

- [ ] **Step 5.4: Commit**

```bash
git add src/ui/Sidebar.h src/ui/Sidebar.cpp
git commit -m "feat: update Sidebar to show Streaming state from CameraStream"
```

---

## Task 6: Update main.cpp — wire everything together

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

#include "oak/Device.h"
#include "oak/CameraStream.h"
#include "ui/Sidebar.h"
#include "ui/StreamView.h"

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
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // OpenGL context is now active — safe to create GL resources
    oak::OakDevice   device;
    oak::CameraStream stream;
    stream.open();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Poll device state and camera frames — before ImGui frame
        device.poll();
        stream.poll();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-screen dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_PassthruCentralNode);

        // Render UI panels
        ui::renderSidebar(device, stream);
        ui::renderStreamView(stream);

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

    // Cleanup — close stream before ImGui/GLFW teardown
    stream.close();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
```

- [ ] **Step 6.2: Build — expect 0 errors**

In VS2022: Ctrl+Shift+B
Expected: **0 errors, 0 warnings** (or only innocuous depthai warnings).
If there are errors, read the error output carefully — most likely a missing include or signature mismatch. Fix before continuing.

- [ ] **Step 6.3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire CameraStream and StreamView into main render loop"
```

---

## Task 7: Milestone verification

**No file changes — runtime testing only.**

- [ ] **Step 7.1: Launch with device plugged in**

Run with F5 (debug) or Ctrl+F5 from VS2022.

Expected:
- Window opens
- `CameraStream: opened, streaming 1280x720 @ 30fps` appears in the Output / stderr
- Sidebar shows green "Streaming" label
- "Camera" window shows live RGB video from the OAK

- [ ] **Step 7.2: Verify aspect ratio**

The camera image should be 16:9 (wider than tall). If it appears square or distorted, the aspect calculation is wrong — check `stream.height() / stream.width()` in StreamView.cpp.

- [ ] **Step 7.3: Verify no-device launch**

Unplug the OAK. Rebuild and run.

Expected:
- App launches without crashing
- `CameraStream::open failed: ...` logged to stderr
- Sidebar shows "No device" (red)
- Camera window shows "No stream"

- [ ] **Step 7.4: Final commit and tag**

```bash
git add -A
git commit -m "feat: stage 2 complete — live RGB camera stream in ImGui window"
git tag stage-2-complete
git push
git push origin stage-2-complete
```

- [ ] **Step 7.5: Update docs/reference/oak-stage2.html**

Copy the updated `E:\luxonis\oak-stage2.html` (if it has been updated with gotchas and annotated code from this stage) into `docs/reference/oak-stage2.html` and commit:

```bash
git add docs/reference/oak-stage2.html
git commit -m "docs: update oak-stage2.html with implementation notes"
git push
```
