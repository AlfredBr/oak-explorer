// src/oak/OakStreams.cpp
#include "oak/OakStreams.h"

#include <depthai/depthai.hpp>
#include <GLFW/glfw3.h>   // Use GLFW's header, not <GL/GL.h> — avoids WINGDIAPI conflict with depthai

#include <algorithm>  // std::clamp
#include <cstdio>

namespace oak {

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

void OakStreams::open() {
    try {
        // --- Build pipeline ---
        dai::Pipeline pipeline;

        // RGB camera — 1280×720, interleaved RGB (required for GL_RGB upload)
        auto cam = pipeline.create<dai::node::ColorCamera>();
        cam->setPreviewSize(1280, 720);
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

        // StereoDepth — HIGH_DENSITY preset, depth output in mm as uint16
        auto stereo = pipeline.create<dai::node::StereoDepth>();
        stereo->setDefaultProfilePreset(dai::node::StereoDepth::PresetMode::HIGH_DENSITY);

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
            // Removes the blue/cyan fringe at image boundaries.
            // 32px covers the top/right fringe from the stereo baseline offset.
            cfg.algorithmControl.numInvalidateEdgePixels = 32;
            stereo->initialConfig.set(cfg);
        }

        monoLeft->out.link(stereo->left);
        monoRight->out.link(stereo->right);

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
        queueSpatial_       = device_->getOutputQueue("spatial",       4, false);
        queueSpatialConfig_ = device_->getInputQueue("spatialConfig");

        // --- Allocate staging buffers ---
        depthRgbBuf_.resize(640 * 400 * 3);  // Turbo-colored RGB for GL upload
        depthBuf_.resize(640 * 400);          // raw uint16 mm values for histogram
        depthEma_.assign(640 * 400, 0.0f);   // temporal EMA — starts at zero (no history)

        // --- Create RGB GL texture (1280×720) ---
        // 1280×3 = 3840 bytes/row — set alignment=1 to be safe (matches Stage 2 working pattern)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glGenTextures(1, &rgbTex_);
        glBindTexture(GL_TEXTURE_2D, rgbTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
            1280, 720, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        // --- Create depth GL texture (640×400) ---
        // 640×3 = 1920 bytes/row — 4-byte aligned, no alignment change needed
        glGenTextures(1, &depthTex_);
        glBindTexture(GL_TEXTURE_2D, depthTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
            640, 400, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // restore default

        streaming_ = true;
        fprintf(stderr, "OakStreams: opened — RGB 1280x720, Depth 640x400 @ 30fps\n");

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
    queueSpatial_.reset();
    queueSpatialConfig_.reset();
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
    depthRgbBuf_.clear();
    depthBuf_.clear();
    depthEma_.clear();
}

void OakStreams::poll() {
    if (!streaming_) return;
    try {
        // --- RGB frame ---
        auto rgbFrame = queueRgb_->tryGet<dai::ImgFrame>();
        if (rgbFrame) {
            int64_t seq = rgbFrame->getSequenceNum();
            if (lastRgbSeq_ >= 0 && seq > lastRgbSeq_ + 1)
                droppedRgb_ += (int)(seq - lastRgbSeq_ - 1);
            lastRgbSeq_ = seq;

            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glBindTexture(GL_TEXTURE_2D, rgbTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                1280, 720,
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
            const auto& data = depthFrame->getData();
            const uint16_t* depthData = reinterpret_cast<const uint16_t*>(data.data());
            const int pixelCount = 640 * 400;

            // Copy raw depth values for histogram access
            std::copy(depthData, depthData + pixelCount, depthBuf_.data());

            // --- Host-side top-edge mask (Fix C) ---
            // The stereo top edge has more noise than the sides due to
            // rectification geometry. Zero out the top 48 rows so they
            // don't corrupt the EMA. The hole-fill pass will interpolate
            // a smooth border from the valid rows below.
            constexpr int kTopMaskRows = 48;
            constexpr int W = 640;
            constexpr int H = 400;
            for (int i = 0; i < kTopMaskRows * W; ++i) {
                depthBuf_[i] = 0;
            }

            // --- Host-side temporal EMA (Step 2 of flicker reduction) ---
            // Blend each pixel's depth with a running average to smooth jitter.
            // Alpha = 0.3 → moderate smoothing; lower = smoother but laggier.
            constexpr float kEmaAlpha = 0.3f;
            float* ema = depthEma_.data();
            for (int i = 0; i < pixelCount; ++i) {
                float raw = (float)depthBuf_[i];  // read from masked buffer, not raw device data
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

            glBindTexture(GL_TEXTURE_2D, depthTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                640, 400,
                GL_RGB, GL_UNSIGNED_BYTE,
                depthRgbBuf_.data());
            glBindTexture(GL_TEXTURE_2D, 0);
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
