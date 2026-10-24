// src/oak/OakStreams.cpp
#include "oak/OakStreams.h"

#include <depthai/depthai.hpp>
#include <GLFW/glfw3.h>   // Use GLFW's header, not <GL/GL.h> — avoids WINGDIAPI conflict with depthai

#include <algorithm>  // std::clamp
#include <chrono>
#include <cstdio>
#include <filesystem>

#include "detect/HostDetector.h"

#ifdef OAK_USE_CUDA
#include "cuda/DepthProcessor.h"
#endif

namespace oak {

// Windows' OpenGL header only declares 1.1 constants. These texture formats
// and swizzles are core in the OpenGL 3.3 context created by main().
static constexpr GLenum kTextureSwizzleG = 0x8E43;
static constexpr GLenum kTextureSwizzleB = 0x8E44;
static constexpr GLint kR8 = 0x8229;

// Turbo colormap LUT — 256 entries, uint8 RGB
// Index 0 = far (10000mm) = dark purple
// Index 255 = close (400mm) = dark red
// Source: canonical Turbo colormap by Anton Mikhailov (Google)
// Verify against https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f
// if colormap output looks wrong (first 128 entries below are canonical).
static constexpr uint8_t kTurboLut[256][3] = {
    // idx 0-127: dark purple → blue → cyan → green → yellow
    {48,18,59},{50,21,67},{51,24,74},{52,27,81},{53,30,88},{54,33,95},{55,36,102},{56,39,109},
    {57,42,115},{58,45,121},{59,47,128},{60,50,134},{61,53,139},{62,56,145},{63,59,150},{63,62,156},
    {64,64,161},{65,67,166},{65,70,171},{66,73,176},{67,75,181},{67,78,186},{68,81,191},{68,84,195},
    {69,86,199},{70,89,203},{70,92,207},{71,95,211},{71,97,215},{72,100,218},{72,103,222},{73,106,225},
    {74,108,228},{74,111,231},{75,114,234},{75,116,237},{76,119,240},{76,122,242},{77,124,244},{78,127,246},
    {79,130,248},{79,132,250},{80,135,251},{80,138,252},{81,140,253},{82,143,254},{83,145,254},{84,148,254},
    {85,150,254},{86,153,253},{87,155,252},{88,158,251},{90,160,250},{91,163,248},{92,165,246},{94,168,244},
    {95,170,242},{97,172,240},{99,175,237},{100,177,235},{102,179,232},{104,181,229},{106,184,226},{107,186,223},
    {109,188,220},{111,190,217},{113,192,214},{115,194,211},{117,196,207},{119,198,204},{121,200,201},{123,202,197},
    {125,203,194},{127,205,190},{129,207,187},{131,208,183},{133,210,179},{135,211,176},{137,213,172},{139,214,168},
    {141,216,164},{143,217,161},{145,218,157},{147,220,153},{149,221,149},{151,222,145},{153,223,141},{155,224,137},
    {157,225,133},{160,226,129},{162,227,125},{164,228,121},{166,229,117},{168,230,113},{170,231,109},{172,232,105},
    {174,232,101},{176,233,97},{178,234,93},{180,235,89},{182,235,85},{184,236,81},{186,237,77},{188,237,73},
    {190,238,69},{192,239,65},{194,239,61},{196,240,57},{198,240,53},{200,241,49},{202,241,45},{204,242,41},
    {206,242,37},{208,243,33},{210,243,29},{212,244,25},{214,244,22},{216,244,18},{218,245,14},{220,245,10},
    {222,246,7},{224,246,4},{226,246,2},{228,247,0},{230,247,0},{232,248,0},{234,248,0},{236,248,0},
    // idx 128-255: yellow → orange → dark red
    {238,249,0},{239,249,0},{241,249,0},{243,249,0},{245,249,0},{246,249,0},{248,249,0},{250,248,0},
    {251,248,0},{253,248,0},{254,247,0},{255,247,0},{255,246,0},{255,245,0},{255,244,0},{255,243,0},
    {255,242,0},{254,241,0},{254,239,0},{254,238,0},{254,237,0},{253,235,0},{253,234,0},{252,232,0},
    {252,231,0},{251,229,0},{251,227,0},{250,226,0},{250,224,0},{249,222,0},{248,220,0},{248,218,0},
    {247,216,0},{246,214,0},{246,212,0},{245,210,0},{244,208,0},{243,206,0},{242,204,0},{241,202,0},
    {241,200,0},{240,198,0},{239,195,0},{238,193,0},{237,191,0},{236,188,0},{235,186,0},{234,183,0},
    {233,181,0},{232,178,0},{231,176,0},{230,173,0},{229,170,0},{228,168,0},{227,165,0},{226,162,0},
    {224,159,0},{223,156,0},{222,153,0},{220,150,0},{219,147,0},{217,144,0},{216,141,0},{214,138,0},
    {213,135,0},{211,132,0},{209,129,0},{208,125,0},{206,122,0},{204,119,0},{202,115,0},{201,112,0},
    {199,108,0},{197,105,0},{195,101,0},{193,97,0},{191,94,0},{189,90,0},{187,86,0},{185,82,0},
    {183,78,0},{181,74,0},{179,70,0},{177,66,0},{174,62,0},{172,58,0},{170,54,0},{167,50,0},
    {165,45,0},{162,41,0},{160,36,0},{157,32,0},{154,27,0},{152,22,0},{149,17,0},{146,12,0},
    {143,7,0},{140,3,0},{137,0,0},{134,0,0},{131,0,0},{128,0,0},{125,0,0},{122,0,0},
    {119,0,0},{116,0,0},{113,0,0},{110,0,0},{107,0,0},{104,0,0},{101,0,0},{98,0,0},
    {95,0,0},{92,0,0},{89,0,0},{86,0,0},{83,0,0},{80,0,0},{77,0,0},{74,0,0},
    {71,0,0},{67,0,0},{63,0,0},{58,0,0},{53,0,0},{47,0,0},{40,0,0},{30,0,0},
};

// Network input size. The 16:9 RGB preview is letterboxed into this square.
static constexpr int kNnSize = 416;

static const char* const kCocoLabels[80] = {
    "person", "bicycle", "car", "motorbike", "aeroplane", "bus", "train", "truck", "boat",
    "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
    "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
    "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
    "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
    "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair",
    "sofa", "pottedplant", "bed", "diningtable", "toilet", "tvmonitor", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator",
    "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush",
};

const char* detectionLabelName(int label) {
    return (label >= 0 && label < 80) ? kCocoLabels[label] : "?";
}

void OakStreams::setRoi(float normX, float normY) {
    if (!streaming_) return;

    // Clamp to valid range — keep the 5% half-width inside the frame
    normX = std::clamp(normX, 0.025f, 0.975f);
    normY = std::clamp(normY, 0.025f, 0.975f);
    roiCenterX_ = normX;
    roiCenterY_ = normY;

    dai::SpatialLocationCalculatorConfig cfg;
    dai::SpatialLocationCalculatorConfigData roi;
    roi.roi = dai::Rect(dai::Point2f(normX - 0.025f, normY - 0.025f),
                        dai::Point2f(normX + 0.025f, normY + 0.025f));
    roi.calculationAlgorithm = dai::SpatialLocationCalculatorAlgorithm::MEDIAN;
    cfg.addROI(roi);

    try {
        queueSpatialConfig_->send(cfg);
    } catch (...) {
        // Non-fatal — spatial result simply won't update this frame
    }
}

OakStreams::OakStreams()  = default;
OakStreams::~OakStreams() { close(); }

void OakStreams::setDetectorSource(DetectorSource source) {
    source_ = source;
    if (source != DetectorSource::HostGpu || hostState_ != HostDetectorState::Idle) return;

    // Session creation compiles the model for the GPU and takes a few seconds,
    // so it runs off the render thread.
    hostState_ = HostDetectorState::Loading;
    hostLoad_ = std::async(std::launch::async, [this, path = hostModelPath_]() {
        std::string err;
        auto detector = detect::HostDetector::create(path, err);
        hostLoadError_ = err;
        return detector;
    });
}

void OakStreams::updateHostLoad() {
    if (hostState_ != HostDetectorState::Loading) return;
    if (hostLoad_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    host_ = hostLoad_.get();
    if (host_) {
        hostState_ = HostDetectorState::Ready;
        fprintf(stderr, "OakStreams: host GPU detector ready\n");
    } else {
        hostState_ = HostDetectorState::Failed;
        hostError_ = hostLoadError_;
        fprintf(stderr, "OakStreams: host GPU detector failed: %s\n", hostError_.c_str());
    }
}

// True when the median valid depth in the middle of the box is beyond the squelch limit.
// A box with no valid depth readings is kept.
bool OakStreams::boxBeyondSquelch(const Detection& d) const {
    if (depthW_ != 1920 || depthH_ != 1080 || depthBuf_.empty()) return false;
    const float w = d.xmax - d.xmin, h = d.ymax - d.ymin;
    const int x0 = (int)((d.xmin + w * 0.25f) * depthW_), x1 = (int)((d.xmax - w * 0.25f) * depthW_);
    const int y0 = (int)((d.ymin + h * 0.25f) * depthH_), y1 = (int)((d.ymax - h * 0.25f) * depthH_);
    const int step = std::max(1, std::max(x1 - x0, y1 - y0) / 24);
    std::vector<uint16_t> samples;
    for (int y = std::clamp(y0, 0, depthH_ - 1); y < std::min(y1 + 1, depthH_); y += step)
        for (int x = std::clamp(x0, 0, depthW_ - 1); x < std::min(x1 + 1, depthW_); x += step) {
            const uint16_t v = depthBuf_[size_t(y) * depthW_ + x];
            if (v) samples.push_back(v);
        }
    if (samples.empty()) return false;
    std::nth_element(samples.begin(), samples.begin() + samples.size() / 2, samples.end());
    return samples[samples.size() / 2] > squelchMaxMm_;
}

bool OakStreams::hasDetector() const {
    return source_ == DetectorSource::Vpu ? hasDetector_ : hostState_ == HostDetectorState::Ready;
}

const std::vector<Detection>& OakStreams::detections() const {
    return source_ == DetectorSource::Vpu ? detections_ : hostDetections_;
}

float OakStreams::detectionFps() const {
    if (source_ == DetectorSource::Vpu) return detFps_;
    return host_ ? host_->fps() : 0.0f;
}

float OakStreams::hostInferMs() const { return host_ ? host_->inferMs() : 0.0f; }

bool OakStreams::usingCuda() const {
#ifdef OAK_USE_CUDA
    return gpuDepth_ && gpuDepth_->ready();
#else
    return false;
#endif
}

void OakStreams::open() {
    close();
    droppedRgb_ = 0;
    droppedDepth_ = 0;
    lastRgbSeq_ = -1;
    lastDepthSeq_ = -1;

    try {
        // --- Build pipeline ---
        dai::Pipeline pipeline;

        // RGB camera — 1920×1080, interleaved RGB (required for GL_RGB upload)
        auto cam = pipeline.create<dai::node::ColorCamera>();
        cam->setPreviewSize(1920, 1080);
        cam->setInterleaved(true);   // REQUIRED for GL_RGB — false outputs planar CHW
        cam->setColorOrder(dai::ColorCameraProperties::ColorOrder::RGB);
        cam->setFps(30);
        // Lock focus after initial acquisition — prevents the periodic re-hunt
        // that blurs the image every few seconds (default is CONTINUOUS_VIDEO).
        cam->initialControl.setAutoFocusMode(dai::CameraControl::AutoFocusMode::AUTO);

        auto xoutRgb = pipeline.create<dai::node::XLinkOut>();
        xoutRgb->setStreamName("rgb");
        cam->preview.link(xoutRgb->input);

        // Stereo cameras — 640×400 each
        auto monoLeft  = pipeline.create<dai::node::MonoCamera>();
        auto monoRight = pipeline.create<dai::node::MonoCamera>();

        monoLeft->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        monoLeft->setBoardSocket(dai::CameraBoardSocket::CAM_B);  // left on OAK-D-Lite

        monoRight->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        monoRight->setBoardSocket(dai::CameraBoardSocket::CAM_C);  // right on OAK-D-Lite

        auto xoutLeft = pipeline.create<dai::node::XLinkOut>();
        auto xoutRight = pipeline.create<dai::node::XLinkOut>();
        xoutLeft->setStreamName("left");
        xoutRight->setStreamName("right");
        xoutLeft->input.setBlocking(false);
        xoutRight->input.setBlocking(false);
        monoLeft->out.link(xoutLeft->input);
        monoRight->out.link(xoutRight->input);

        // StereoDepth — HIGH_DENSITY preset, depth output in mm as uint16
        auto stereo = pipeline.create<dai::node::StereoDepth>();
        stereo->setDefaultProfilePreset(dai::node::StereoDepth::PresetMode::HIGH_DENSITY);
        // Align depth to the RGB camera's perspective so objects appear
        // in the same position in both the Camera and Depth views.
        stereo->setDepthAlign(dai::CameraBoardSocket::CAM_A);

        // --- On-device depth cleanup filters (Step 1 of flicker reduction) ---
        // Left-Right consistency check: discards pixels where L→R and R→L disagree
        stereo->setLeftRightCheck(true);
        // Median filter: smooths spatial noise without shifting edges much
        stereo->initialConfig.setMedianFilter(dai::MedianFilter::KERNEL_7x7);
        // Enable speckle + temporal filters via the raw config
        {
            auto cfg = stereo->initialConfig.get();
            cfg.postProcessing.speckleFilter.enable = true;
            cfg.postProcessing.speckleFilter.speckleRange = 50;
            cfg.postProcessing.temporalFilter.enable = true;
            // Invalidate edge pixels where stereo has no valid disparity.
            // This operates in the stereo-native resolution (640×400), not the
            // aligned output. 32px was calibrated for 640-wide native space.
            cfg.algorithmControl.numInvalidateEdgePixels = 32;
            stereo->initialConfig.set(cfg);
        }

        monoLeft->out.link(stereo->left);
        monoRight->out.link(stereo->right);

        // --- Object detection (optional): RGB preview -> letterbox -> YOLO on the VPU ---
        const bool wantDetector = !modelPath_.empty() && std::filesystem::exists(modelPath_);
        if (!modelPath_.empty() && !wantDetector)
            fprintf(stderr, "OakStreams: model not found, detection disabled: %s\n", modelPath_.c_str());
        if (wantDetector) {
            auto manip = pipeline.create<dai::node::ImageManip>();
            manip->initialConfig.setResizeThumbnail(kNnSize, kNnSize);
            manip->initialConfig.setFrameType(dai::ImgFrame::Type::BGR888p);
            manip->setMaxOutputFrameSize(kNnSize * kNnSize * 3);
            manip->inputImage.setBlocking(false);
            manip->inputImage.setQueueSize(1);
            cam->preview.link(manip->inputImage);

            // YOLOv6n, COCO 80 classes (anchor-free, so no anchors/masks)
            auto yolo = pipeline.create<dai::node::YoloDetectionNetwork>();
            yolo->setBlobPath(modelPath_);
            yolo->setConfidenceThreshold(kMinDetectionConfidence);
            yolo->setNumClasses(80);
            yolo->setCoordinateSize(4);
            yolo->setAnchors({});
            yolo->setAnchorMasks({});
            yolo->setIouThreshold(0.5f);
            yolo->setNumInferenceThreads(2);
            yolo->input.setBlocking(false);
            yolo->input.setQueueSize(1);
            manip->out.link(yolo->input);

            auto xoutDet = pipeline.create<dai::node::XLinkOut>();
            xoutDet->setStreamName("detections");
            yolo->out.link(xoutDet->input);
        }

        auto xoutDepth = pipeline.create<dai::node::XLinkOut>();
        xoutDepth->setStreamName("depth");
        stereo->depth.link(xoutDepth->input);   // depth output, NOT disparity — uint16 mm

        // --- SpatialLocationCalculator ---
        // Wired to stereo depth; host sends ROI configs via XLinkIn.
        auto spatialCalc = pipeline.create<dai::node::SpatialLocationCalculator>();
        spatialCalc->setWaitForConfigInput(false);  // use initialConfig until first update

        // Initial ROI at image centre (5%×5%)
        dai::SpatialLocationCalculatorConfigData initialRoi;
        initialRoi.roi = dai::Rect(dai::Point2f(0.475f, 0.475f),
                                   dai::Point2f(0.525f, 0.525f));
        initialRoi.calculationAlgorithm =
            dai::SpatialLocationCalculatorAlgorithm::MEDIAN;
        spatialCalc->initialConfig.addROI(initialRoi);

        stereo->depth.link(spatialCalc->inputDepth);

        // XLinkIn — receives SpatialLocationsConfig packets from host
        auto spatialConfigIn = pipeline.create<dai::node::XLinkIn>();
        spatialConfigIn->setStreamName("spatialConfig");
        spatialConfigIn->out.link(spatialCalc->inputConfig);

        // XLinkOut — sends SpatialLocationData packets to host
        auto xoutSpatial = pipeline.create<dai::node::XLinkOut>();
        xoutSpatial->setStreamName("spatial");
        spatialCalc->out.link(xoutSpatial->input);

        // --- Open device (boots pipeline onto VPU) ---
        device_ = std::make_unique<dai::Device>(pipeline);

        // --- Get output queues (non-blocking, 4-frame buffer) ---
        queueRgb_   = device_->getOutputQueue("rgb",   4, false);
        queueDepth_ = device_->getOutputQueue("depth", 4, false);
        queueLeft_ = device_->getOutputQueue("left", 4, false);
        queueRight_ = device_->getOutputQueue("right", 4, false);
        queueSpatial_       = device_->getOutputQueue("spatial",       4, false);
        queueSpatialConfig_ = device_->getInputQueue("spatialConfig");
        if (wantDetector) {
            queueDet_ = device_->getOutputQueue("detections", 2, false);
            hasDetector_ = true;
        }

        // --- Create RGB
        // 1920×3 = 5760 bytes/row — set alignment=1 to be safe (matches Stage 2 working pattern)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glGenTextures(1, &rgbTex_);
        glBindTexture(GL_TEXTURE_2D, rgbTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
            1920, 1080, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // restore default

        // Depth buffers and GL texture are created lazily on the first depth frame
        // because setDepthAlign() may change the output resolution to match the
        // aligned camera (e.g., RGB at 1920×1080 instead of stereo-native 640×400).
        depthW_ = 0;
        depthH_ = 0;

        streaming_ = true;
        fprintf(stderr, "OakStreams: opened — RGB 1920x1080, Depth size determined on first frame\n");

    } catch (const std::exception& e) {
        fprintf(stderr, "OakStreams::open failed: %s\n", e.what());
        close();
    } catch (...) {
        fprintf(stderr, "OakStreams::open failed: unknown exception\n");
        close();
    }
}

void OakStreams::close() {
    streaming_ = false;
    queueRgb_.reset();           // release queues before device
    queueDepth_.reset();
    queueLeft_.reset();
    queueRight_.reset();
    pendingLeft_.reset();
    pendingRight_.reset();
    queueSpatial_.reset();
    queueSpatialConfig_.reset();
    queueDet_.reset();
    hasDetector_ = false;
    detections_.clear();
    hostDetections_.clear();
    detFps_ = 0.0f;
    detCount_ = 0;
    detWindowStart_ = 0.0;
    device_.reset();             // destructor stops pipeline
    hasSpatial_  = false;
    roiCenterX_  = 0.5f;
    roiCenterY_  = 0.5f;
    if (rgbTex_ != 0) {
        glDeleteTextures(1, &rgbTex_);
        rgbTex_ = 0;
    }
    if (depthTex_ != 0) {
        glDeleteTextures(1, &depthTex_);
        depthTex_ = 0;
    }
    // The GPU path page-locks depthBuf_/depthRgbBuf_, so it must go first.
#ifdef OAK_USE_CUDA
    gpuDepth_.reset();
#endif
    depthRgbBuf_.clear();
    depthBuf_.clear();
    depthEma_.clear();
    depthProcessMs_ = 0.0f;
    depthMsAccum_   = 0.0;
    depthMsFrames_  = 0;
    if (leftTex_) glDeleteTextures(1, &leftTex_);
    if (rightTex_) glDeleteTextures(1, &rightTex_);
    leftTex_ = rightTex_ = 0;
    stereoW_ = stereoH_ = 0;
}

void OakStreams::poll() {
    updateHostLoad();
    if (!streaming_) return;
    try {
        // Only publish matching stereo pairs. Bound queue draining so a lagging
        // camera cannot block the render loop; retain unmatched frames for later.
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (!pendingLeft_) pendingLeft_ = queueLeft_->tryGet<dai::ImgFrame>();
            if (!pendingRight_) pendingRight_ = queueRight_->tryGet<dai::ImgFrame>();
            if (!pendingLeft_ || !pendingRight_) break;
            if (pendingLeft_->getSequenceNum() < pendingRight_->getSequenceNum()) {
                pendingLeft_.reset();
                continue;
            }
            if (pendingRight_->getSequenceNum() < pendingLeft_->getSequenceNum()) {
                pendingRight_.reset();
                continue;
            }
            const int w = pendingLeft_->getWidth(), h = pendingLeft_->getHeight();
            if (pendingRight_->getWidth() == w && pendingRight_->getHeight() == h &&
                pendingLeft_->getData().size() >= size_t(w) * h &&
                pendingRight_->getData().size() >= size_t(w) * h) {
                const bool resize = w != stereoW_ || h != stereoH_;
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                auto upload = [&](GLuint& texture, const dai::ImgFrame& frame) {
                    if (!texture) glGenTextures(1, &texture);
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    // Expand mono to RGB through texture swizzles (OpenGL 3.3).
                    glTexParameteri(GL_TEXTURE_2D, kTextureSwizzleG, GL_RED);
                    glTexParameteri(GL_TEXTURE_2D, kTextureSwizzleB, GL_RED);
                    if (resize)
                        glTexImage2D(GL_TEXTURE_2D, 0, kR8, w, h, 0, GL_RED,
                                     GL_UNSIGNED_BYTE, frame.getData().data());
                    else
                        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED,
                                        GL_UNSIGNED_BYTE, frame.getData().data());
                };
                upload(leftTex_, *pendingLeft_);
                upload(rightTex_, *pendingRight_);
                if (resize) fprintf(stderr, "OakStreams: stereo pair size = %dx%d\n", w, h);
                stereoW_ = w;
                stereoH_ = h;
                glBindTexture(GL_TEXTURE_2D, 0);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            }
            pendingLeft_.reset();
            pendingRight_.reset();
        }
        // --- RGB frame ---
        auto rgbFrame = queueRgb_->tryGet<dai::ImgFrame>();
        if (rgbFrame) {
            int64_t seq = rgbFrame->getSequenceNum();
            if (lastRgbSeq_ >= 0 && seq > lastRgbSeq_ + 1)
                droppedRgb_ += (int)(seq - lastRgbSeq_ - 1);
            lastRgbSeq_ = seq;

            auto& rgbData = rgbFrame->getData();
            const bool fullHd = rgbData.size() >= size_t(1920) * 1080 * 3;

            // Unless the squelch is meant to limit detection too, detect on the untouched frame.
            const bool hostActive = host_ && source_ == DetectorSource::HostGpu && fullHd;
            const bool squelchActive = squelch_ && fullHd && depthW_ == 1920 && depthH_ == 1080;
            if (hostActive && !(squelchActive && squelchDetection_))
                host_->submit(rgbData.data(), 1920, 1080);

            // Depth is aligned to the RGB camera, so pixels correspond 1:1 at 1920x1080.
            if (squelchActive) {
                const uint16_t* depth = depthBuf_.data();
                uint8_t* px = rgbData.data();
                const uint16_t maxMm = (uint16_t)std::clamp(squelchMaxMm_, 0.0f, 65535.0f);
                const size_t n = size_t(1920) * 1080;
                for (size_t i = 0; i < n; ++i) {
                    const uint16_t d = depth[i];
                    if (d == 0 ? squelchHideUnknown_ : d > maxMm)
                        px[i * 3] = px[i * 3 + 1] = px[i * 3 + 2] = 0;
                }
                // The host model then only sees pixels inside the depth limit.
                if (hostActive && squelchDetection_)
                    host_->submit(rgbData.data(), 1920, 1080);
            }

            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glBindTexture(GL_TEXTURE_2D, rgbTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                1920, 1080,
                GL_RGB, GL_UNSIGNED_BYTE,
                rgbFrame->getData().data());
            glBindTexture(GL_TEXTURE_2D, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }

        // --- Depth frame ---
        auto depthFrame = queueDepth_->tryGet<dai::ImgFrame>();
        if (depthFrame) {
            // Note: sequence-gap dropped-frame counting is disabled for depth.
            // The on-device temporal filter consumes multiple input frames per
            // output, making sequence gaps expected rather than indicative of
            // actual data loss.

            // --- Lazy init: size buffers and GL texture from the actual frame ---
            // setDepthAlign() can change the depth output resolution to match the
            // aligned camera, so we derive dimensions from the real frame rather
            // than hardcoding stereo-native 640×400.
            const int W = (int)depthFrame->getWidth();
            const int H = (int)depthFrame->getHeight();
            if (W != depthW_ || H != depthH_) {
                depthW_ = W;
                depthH_ = H;
                const int pc = W * H;
#ifdef OAK_USE_CUDA
                gpuDepth_.reset();  // unpin the old buffers before they are reallocated
#endif
                depthRgbBuf_.resize(pc * 3);
                depthBuf_.resize(pc);
                depthEma_.assign(pc, 0.0f);
#ifdef OAK_USE_CUDA
                if (cuda::DepthProcessor::available()) {
                    auto proc = std::make_unique<cuda::DepthProcessor>();
                    if (proc->init(W, H, &kTurboLut[0][0], depthBuf_.data(), depthRgbBuf_.data())) {
                        gpuDepth_ = std::move(proc);
                        fprintf(stderr, "OakStreams: depth post-processing on CUDA\n");
                    }
                }
                if (!gpuDepth_) fprintf(stderr, "OakStreams: depth post-processing on CPU\n");
#endif
                if (depthTex_ == 0) glGenTextures(1, &depthTex_);
                glBindTexture(GL_TEXTURE_2D, depthTex_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
                glBindTexture(GL_TEXTURE_2D, 0);
                fprintf(stderr, "OakStreams: depth frame size = %d×%d\n", W, H);
            }

            const auto& data = depthFrame->getData();
            const uint16_t* depthData = reinterpret_cast<const uint16_t*>(data.data());
            const int pixelCount = W * H;
            std::copy_n(depthData, pixelCount, depthBuf_.data());
            constexpr float kEmaAlpha = 0.3f;

            const auto procStart = std::chrono::steady_clock::now();
            bool processed = false;
#ifdef OAK_USE_CUDA
            if (gpuDepth_) {
                processed = gpuDepth_->process(kEmaAlpha);
                if (!processed) {
                    fprintf(stderr, "OakStreams: CUDA depth processing failed, falling back to CPU\n");
                    gpuDepth_.reset();
                    depthEma_.assign(pixelCount, 0.0f);
                }
            }
#endif
            if (!processed) {
                float* ema = depthEma_.data();
                for (int i = 0; i < pixelCount; ++i) {
                    float raw = (float)depthData[i];
                    if (raw == 0.0f) {
                        // Invalid pixel — keep previous EMA value (don't corrupt history)
                        continue;
                    }
                    if (ema[i] == 0.0f) {
                        // First valid sample for this pixel — seed directly
                        ema[i] = raw;
                    } else {
                        ema[i] = kEmaAlpha * raw + (1.0f - kEmaAlpha) * ema[i];
                    }
                }

                // --- Host-side hole fill (Step 3 of flicker reduction) ---
                // Fill pixels that have never received a valid depth sample by
                // averaging valid neighbors in a 3×3 window. Single pass, in-place.
                for (int y = 0; y < H; ++y) {
                    for (int x = 0; x < W; ++x) {
                        int idx = y * W + x;
                        if (ema[idx] != 0.0f) continue;  // already valid
                        float sum = 0.0f;
                        int   cnt = 0;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                int nx = x + dx, ny = y + dy;
                                if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                                float v = ema[ny * W + nx];
                                if (v != 0.0f) { sum += v; ++cnt; }
                            }
                        }
                        if (cnt > 0) ema[idx] = sum / (float)cnt;
                    }
                }

                // Apply Turbo colormap using the smoothed EMA values
                uint8_t* rgbBuf = depthRgbBuf_.data();
                for (int i = 0; i < pixelCount; ++i) {
                    float d = ema[i];
                    if (d == 0.0f) {
                        // No valid depth ever seen — black
                        rgbBuf[i*3] = rgbBuf[i*3+1] = rgbBuf[i*3+2] = 0;
                        continue;
                    }
                    int idx = std::clamp(
                        (int)((10000.0f - d) / (10000.0f - 400.0f) * 255.0f),
                        0, 255);
                    rgbBuf[i*3]   = kTurboLut[idx][0];
                    rgbBuf[i*3+1] = kTurboLut[idx][1];
                    rgbBuf[i*3+2] = kTurboLut[idx][2];
                }
            }

            depthMsAccum_ += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - procStart).count();
            if (++depthMsFrames_ == 120) {
                depthProcessMs_ = (float)(depthMsAccum_ / depthMsFrames_);
                fprintf(stderr, "OakStreams: depth processing avg %.2f ms/frame (%s, %dx%d)\n",
                        depthProcessMs_, usingCuda() ? "CUDA" : "CPU", W, H);
                depthMsAccum_ = 0.0;
                depthMsFrames_ = 0;
            }

            glBindTexture(GL_TEXTURE_2D, depthTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                W, H,
                GL_RGB, GL_UNSIGNED_BYTE,
                depthRgbBuf_.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // --- Detections ---
        if (host_) host_->fetch(hostDetections_);
        if (queueDet_) {
            std::shared_ptr<dai::ImgDetections> latest;
            int received = 0;
            while (auto msg = queueDet_->tryGet<dai::ImgDetections>()) { latest = msg; ++received; }
            if (latest) {
                // Undo the letterbox: the 16:9 image fills the full width and the
                // middle band of the square network input.
                constexpr float kImageAspect = 1080.0f / 1920.0f;
                constexpr float kPad = (1.0f - kImageAspect) * 0.5f;
                detections_.clear();
                for (const auto& d : latest->detections) {
                    Detection out;
                    out.xmin = std::clamp(d.xmin, 0.0f, 1.0f);
                    out.xmax = std::clamp(d.xmax, 0.0f, 1.0f);
                    out.ymin = std::clamp((d.ymin - kPad) / kImageAspect, 0.0f, 1.0f);
                    out.ymax = std::clamp((d.ymax - kPad) / kImageAspect, 0.0f, 1.0f);
                    out.confidence = d.confidence;
                    out.label = d.label;
                    if (out.xmax <= out.xmin || out.ymax <= out.ymin) continue;
                    // The camera's network can't be masked, so drop boxes whose object is too far.
                    if (squelch_ && squelchDetection_ && boxBeyondSquelch(out)) continue;
                    detections_.push_back(out);
                }
            }
            detCount_ += received;
            const double now = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (detWindowStart_ == 0.0) detWindowStart_ = now;
            if (now - detWindowStart_ >= 1.0) {
                detFps_ = (float)(detCount_ / (now - detWindowStart_));
                detCount_ = 0;
                detWindowStart_ = now;
            }
        }

        // --- Spatial location result ---
        auto spatialData = queueSpatial_->tryGet<dai::SpatialLocationCalculatorData>();
        if (spatialData) {
            const auto locs = spatialData->getSpatialLocations();
            if (!locs.empty()) {
                const auto& coords = locs[0].spatialCoordinates;
                spatialX_   = coords.x / 1000.0f;  // mm → metres
                spatialY_   = coords.y / 1000.0f;
                spatialZ_   = coords.z / 1000.0f;
                hasSpatial_ = true;
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "OakStreams::poll error: %s\n", e.what());
        streaming_ = false;
    } catch (...) {
        streaming_ = false;
    }
}

} // namespace oak
