// src/detect/HostDetectorStub.cpp
// Used when the app is built without ONNX Runtime (OAK_ONNXRUNTIME_DIR not found).
#include "detect/HostDetector.h"

namespace detect {

struct HostDetector::Impl {};

std::unique_ptr<HostDetector> HostDetector::create(const std::string&, std::string& error) {
	error = "built without ONNX Runtime";
	return nullptr;
}

HostDetector::HostDetector(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
HostDetector::~HostDetector() = default;
bool HostDetector::submit(const uint8_t*, int, int) { return false; }
bool HostDetector::fetch(std::vector<oak::Detection>&) { return false; }
float HostDetector::inferMs() const { return 0.0f; }
float HostDetector::fps() const { return 0.0f; }

} // namespace detect
