// src/detect/HostDetector.h
#pragma once
#include "oak/Detection.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace detect {

// YOLOv8 object detector running on the host GPU through ONNX Runtime (DirectML).
// Inference runs on a worker thread so the render loop never blocks.
class HostDetector {
public:
	// Loads the .onnx model and creates the GPU session. Returns null and fills
	// `error` on failure (missing file, no usable GPU, unsupported model).
	static std::unique_ptr<HostDetector> create(const std::string& modelPath, std::string& error);
	~HostDetector();

	// Hands an interleaved RGB frame to the worker. Non-blocking: if the worker is
	// still busy with the previous frame this one is dropped and false is returned.
	bool submit(const uint8_t* rgb, int width, int height);

	// Copies out the latest result if a new inference finished since the last call.
	bool fetch(std::vector<oak::Detection>& out);

	float inferMs() const;   // smoothed time per inference
	float fps() const;       // completed inferences per second

private:
	struct Impl;
	explicit HostDetector(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

} // namespace detect
