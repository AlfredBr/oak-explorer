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

    // Frames skipped since open() — detected via sequence number gaps in poll().
    int droppedRgbFrames()   const { return droppedRgb_; }
    int droppedDepthFrames() const { return droppedDepth_; }

private:
    std::unique_ptr<dai::Device>          device_;
    std::shared_ptr<dai::DataOutputQueue> queueRgb_;
    std::shared_ptr<dai::DataOutputQueue> queueDepth_;

    GLuint rgbTex_   = 0;
    GLuint depthTex_ = 0;

    std::vector<uint8_t>  depthRgbBuf_;  // 640*400*3 — Turbo-colored staging buffer
    std::vector<uint16_t> depthBuf_;     // 640*400   — raw depth values for histogram

    int     droppedRgb_   = 0;
    int     droppedDepth_ = 0;
    int64_t lastRgbSeq_   = -1;  // -1 = no frame seen yet
    int64_t lastDepthSeq_ = -1;

    bool streaming_ = false;
};

} // namespace oak
