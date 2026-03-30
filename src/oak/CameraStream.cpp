// src/oak/CameraStream.cpp
#include "oak/CameraStream.h"

#include <depthai/depthai.hpp>
#include <GLFW/glfw3.h>   // includes GL with correct Windows setup (replaces GL/GL.h)

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
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // restore default after texture upload

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
