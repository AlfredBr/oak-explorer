// src/oak/OakStreams.h
#pragma once
#include "oak/Detection.h"

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

// Forward-declare GLuint to avoid pulling OpenGL headers into every TU
typedef unsigned int GLuint;

// Forward-declare depthai types used in private members
namespace dai {
    class Device;
    class DataOutputQueue;
    class DataInputQueue;
    class ImgFrame;
}

namespace cuda { class DepthProcessor; }
namespace detect { class HostDetector; }

namespace oak {

// Where object detection runs.
enum class DetectorSource { Vpu, HostGpu };

// Host-GPU model load progress (the model loads on a background thread).
enum class HostDetectorState { Idle, Loading, Ready, Failed };

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

    // Path of the .blob neural network to run on the OAK. Must be set before open().
    // If empty or missing, the pipeline runs without detection.
    void setModelPath(std::string path) { modelPath_ = std::move(path); }

    // Release device and both GL textures.
    // Safe to call at any time, including if open() never succeeded.
    void close();

    // Drain all queues (RGB, depth, spatial), apply Turbo colormap to depth, upload both textures.
    // Never throws. Call once per render frame before ImGui::NewFrame().
    void poll();

    bool isStreaming() const { return streaming_; }

    // GL textures for rendering
    GLuint rgbTextureId()   const { return rgbTex_; }
    GLuint depthTextureId() const { return depthTex_; }
    int    rgbWidth()       const { return 1920; }
    int    rgbHeight()      const { return 1080; }
    int    depthWidth()     const { return depthW_; }
    int    depthHeight()    const { return depthH_; }
    GLuint leftTextureId()  const { return leftTex_; }
    GLuint rightTextureId() const { return rightTex_; }
    int stereoWidth() const { return stereoW_; }
    int stereoHeight() const { return stereoH_; }

    // Raw uint16 depth values in mm, 0 = invalid. Updated each poll().
    // Used by DepthView to build the ImPlot histogram.
    const uint16_t* rawDepthData()    const { return depthBuf_.data(); }
    int             depthPixelCount() const { return depthWidth() * depthHeight(); }

    // Send an updated ROI to the SpatialLocationCalculator.
    // normX, normY are in [0,1] relative to the 1920×1080 RGB image.
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

    // True when the depth colormap/filter stage runs on the GPU.
    bool usingCuda() const;
    // Average host-side depth processing time (ms) over recent frames.
    float depthProcessMs() const { return depthProcessMs_; }

    // --- Object detection ---
    // The VPU detector runs on the camera (needs the .blob from setModelPath());
    // the host detector runs an ONNX model on the PC's GPU.
    void setHostModelPath(std::string path) { hostModelPath_ = std::move(path); }
    // Selecting HostGpu starts loading the host model in the background on first use.
    void setDetectorSource(DetectorSource source);
    DetectorSource detectorSource() const { return source_; }
    bool hasVpuDetector() const { return hasDetector_; }
    HostDetectorState hostState() const { return hostState_; }
    const std::string& hostError() const { return hostError_; }
    // True when the selected source is producing detections.
    bool hasDetector() const;
    // Most recent result of the selected source; replaced by each new inference.
    const std::vector<Detection>& detections() const;
    float detectionFps() const;
    float hostInferMs() const;

    // --- Depth squelch ---
    // Blacks out RGB pixels farther than maxMeters (needs 1920x1080 aligned depth).
    // hideUnknown also blacks out pixels with no valid depth reading.
    // limitDetection also keeps detection from seeing or reporting objects beyond maxMeters.
    void setSquelch(bool enabled, float maxMeters, bool hideUnknown, bool limitDetection) {
        squelch_ = enabled; squelchMaxMm_ = maxMeters * 1000.0f; squelchHideUnknown_ = hideUnknown;
        squelchDetection_ = limitDetection;
    }

private:
    void updateHostLoad();
    bool boxBeyondSquelch(const Detection& d) const;
    std::unique_ptr<dai::Device>          device_;
    std::shared_ptr<dai::DataOutputQueue> queueRgb_;
    std::shared_ptr<dai::DataOutputQueue> queueDepth_;
    std::shared_ptr<dai::DataOutputQueue> queueLeft_, queueRight_;
    std::shared_ptr<dai::ImgFrame> pendingLeft_, pendingRight_;
    GLuint leftTex_ = 0, rightTex_ = 0;
    int stereoW_ = 0, stereoH_ = 0;

    GLuint rgbTex_   = 0;
    GLuint depthTex_ = 0;

    int depthW_ = 0;  // actual depth frame width  — set on first frame
    int depthH_ = 0;  // actual depth frame height — set on first frame

    std::vector<uint8_t>  depthRgbBuf_;  // depthW_*depthH_*3 — Turbo-colored staging buffer
    std::vector<uint16_t> depthBuf_;     // depthW_*depthH_   — raw depth values for histogram
    std::vector<float>    depthEma_;     // depthW_*depthH_   — temporal EMA of depth (float to avoid quantization drift)

    // Optional GPU path (null after a CUDA failure). Only present when built with OAK_ENABLE_CUDA.
#ifdef OAK_USE_CUDA
    std::unique_ptr<cuda::DepthProcessor> gpuDepth_;
#endif
    float depthProcessMs_ = 0.0f;
    double depthMsAccum_  = 0.0;
    int    depthMsFrames_ = 0;

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

    std::string modelPath_;
    std::shared_ptr<dai::DataOutputQueue> queueDet_;
    std::vector<Detection> detections_;      // VPU results
    std::vector<Detection> hostDetections_;  // host-GPU results

    DetectorSource source_ = DetectorSource::Vpu;
    std::string hostModelPath_, hostError_;
    HostDetectorState hostState_ = HostDetectorState::Idle;
    std::future<std::unique_ptr<detect::HostDetector>> hostLoad_;
    std::string hostLoadError_;  // written by the loader thread, read after the future completes
    std::unique_ptr<detect::HostDetector> host_;

    bool  squelch_ = false;
    float squelchMaxMm_ = 3000.0f;
    bool  squelchHideUnknown_ = false;
    bool  squelchDetection_ = true;
    bool  hasDetector_ = false;
    float detFps_ = 0.0f;
    int   detCount_ = 0;
    double detWindowStart_ = 0.0;
};

} // namespace oak
