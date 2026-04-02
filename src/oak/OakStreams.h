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
    class DataInputQueue;
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

    std::shared_ptr<dai::DataInputQueue>  queueSpatialConfig_;
    std::shared_ptr<dai::DataOutputQueue> queueSpatial_;
    float spatialX_    = 0.0f;
    float spatialY_    = 0.0f;
    float spatialZ_    = 0.0f;
    float roiCenterX_  = 0.5f;  // starts at image centre
    float roiCenterY_  = 0.5f;
    bool  hasSpatial_  = false;

    bool streaming_ = false;
};

} // namespace oak
