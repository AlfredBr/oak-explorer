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
