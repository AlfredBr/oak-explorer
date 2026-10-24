// src/detect/HostDetector.cpp
#ifndef NOMINMAX
#define NOMINMAX  // keep std::min/max usable alongside windows.h
#endif
#include "detect/HostDetector.h"

#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <numeric>
#include <thread>

namespace detect {

namespace {

constexpr int   kInputSize = 640;     // YOLOv8 export size
constexpr float kIouThreshold = 0.5f;
constexpr size_t kMaxDetections = 100;

float iou(const oak::Detection& a, const oak::Detection& b) {
	const float ix = std::max(0.0f, std::min(a.xmax, b.xmax) - std::max(a.xmin, b.xmin));
	const float iy = std::max(0.0f, std::min(a.ymax, b.ymax) - std::max(a.ymin, b.ymin));
	const float inter = ix * iy;
	const float uni = (a.xmax - a.xmin) * (a.ymax - a.ymin) +
					  (b.xmax - b.xmin) * (b.ymax - b.ymin) - inter;
	return uni > 0.0f ? inter / uni : 0.0f;
}

// Greedy per-class non-maximum suppression, highest confidence first.
std::vector<oak::Detection> nms(std::vector<oak::Detection> boxes) {
	std::sort(boxes.begin(), boxes.end(),
			  [](const auto& a, const auto& b) { return a.confidence > b.confidence; });
	std::vector<oak::Detection> kept;
	for (const auto& b : boxes) {
		bool suppressed = false;
		for (const auto& k : kept) {
			if (k.label == b.label && iou(k, b) > kIouThreshold) { suppressed = true; break; }
		}
		if (!suppressed) {
			kept.push_back(b);
			if (kept.size() >= kMaxDetections) break;
		}
	}
	return kept;
}

} // namespace

struct HostDetector::Impl {
	Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "oak-explorer"};
	Ort::Session session{nullptr};
	std::string inputName, outputName;

	// Letterboxed 640x640 RGB frame handed over by submit()
	std::vector<uint8_t> frame = std::vector<uint8_t>(size_t(kInputSize) * kInputSize * 3);
	int srcW = 0, srcH = 0;
	float scale = 1.0f, padX = 0.0f, padY = 0.0f;

	std::vector<float> tensor = std::vector<float>(size_t(3) * kInputSize * kInputSize);

	std::thread worker;
	std::mutex mutex;
	std::condition_variable cv;
	bool pending = false;          // frame waiting for the worker
	bool stop = false;
	std::atomic<bool> busy{false}; // set from submit() until the worker finishes

	std::vector<oak::Detection> result;
	bool resultFresh = false;
	std::atomic<float> inferMs{0.0f};
	std::atomic<float> fps{0.0f};

	void run();
	std::vector<oak::Detection> infer();
};

std::vector<oak::Detection> HostDetector::Impl::infer() {
	// HWC uint8 -> CHW float [0,1]
	const size_t plane = size_t(kInputSize) * kInputSize;
	for (size_t i = 0; i < plane; ++i) {
		tensor[i]             = frame[i * 3 + 0] * (1.0f / 255.0f);
		tensor[plane + i]     = frame[i * 3 + 1] * (1.0f / 255.0f);
		tensor[2 * plane + i] = frame[i * 3 + 2] * (1.0f / 255.0f);
	}

	Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
	const int64_t inShape[4] = {1, 3, kInputSize, kInputSize};
	Ort::Value input = Ort::Value::CreateTensor<float>(mem, tensor.data(), tensor.size(), inShape, 4);

	const char* inNames[]  = {inputName.c_str()};
	const char* outNames[] = {outputName.c_str()};
	auto outputs = session.Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);

	// Output is [1, 4 + numClasses, numAnchors]: rows cx, cy, w, h, then class scores.
	const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
	if (shape.size() != 3) return {};
	const int rows = (int)shape[1];
	const int count = (int)shape[2];
	const int numClasses = rows - 4;
	const float* data = outputs[0].GetTensorData<float>();

	std::vector<oak::Detection> candidates;
	for (int i = 0; i < count; ++i) {
		int best = 0;
		float bestScore = 0.0f;
		for (int c = 0; c < numClasses; ++c) {
			const float s = data[size_t(4 + c) * count + i];
			if (s > bestScore) { bestScore = s; best = c; }
		}
		if (bestScore < oak::kMinDetectionConfidence) continue;

		const float cx = data[i], cy = data[size_t(count) + i];
		const float w = data[size_t(2) * count + i], h = data[size_t(3) * count + i];

		// Letterbox pixels -> normalised coordinates of the original image
		const float imgW = srcW * scale, imgH = srcH * scale;
		oak::Detection d;
		d.xmin = std::clamp((cx - w * 0.5f - padX) / imgW, 0.0f, 1.0f);
		d.xmax = std::clamp((cx + w * 0.5f - padX) / imgW, 0.0f, 1.0f);
		d.ymin = std::clamp((cy - h * 0.5f - padY) / imgH, 0.0f, 1.0f);
		d.ymax = std::clamp((cy + h * 0.5f - padY) / imgH, 0.0f, 1.0f);
		d.confidence = bestScore;
		d.label = best;
		if (d.xmax > d.xmin && d.ymax > d.ymin) candidates.push_back(d);
	}
	return nms(std::move(candidates));
}

void HostDetector::Impl::run() {
	using clock = std::chrono::steady_clock;
	auto windowStart = clock::now();
	int windowCount = 0;

	for (;;) {
		{
			std::unique_lock<std::mutex> lock(mutex);
			cv.wait(lock, [&] { return pending || stop; });
			if (stop) return;
			pending = false;
		}

		std::vector<oak::Detection> out;
		const auto t0 = clock::now();
		try {
			out = infer();
		} catch (const std::exception& e) {
			fprintf(stderr, "HostDetector: inference failed: %s\n", e.what());
		}
		const float ms = std::chrono::duration<float, std::milli>(clock::now() - t0).count();
		const float prev = inferMs.load();
		inferMs = prev == 0.0f ? ms : prev * 0.9f + ms * 0.1f;

		{
			std::lock_guard<std::mutex> lock(mutex);
			result = std::move(out);
			resultFresh = true;
		}
		busy = false;

		++windowCount;
		const double secs = std::chrono::duration<double>(clock::now() - windowStart).count();
		if (secs >= 1.0) {
			fps = (float)(windowCount / secs);
			windowCount = 0;
			windowStart = clock::now();
		}
	}
}

std::unique_ptr<HostDetector> HostDetector::create(const std::string& modelPath, std::string& error) {
	try {
		if (!std::filesystem::exists(modelPath)) {
			error = "model not found: " + modelPath;
			return nullptr;
		}

		auto impl = std::make_unique<Impl>();

		Ort::SessionOptions options;
		options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		// Required by the DirectML provider
		options.DisableMemPattern();
		options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

		const OrtDmlApi* dml = nullptr;
		Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi(
			"DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml)));
		// Pick the fastest discrete GPU rather than whatever adapter enumerates first
		OrtDmlDeviceOptions devOpts{};
		devOpts.Preference = OrtDmlPerformancePreference::HighPerformance;
		devOpts.Filter = OrtDmlDeviceFilter::Gpu;
		Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML2(options, &devOpts));

		impl->session = Ort::Session(impl->env, std::filesystem::path(modelPath).c_str(), options);

		Ort::AllocatorWithDefaultOptions alloc;
		impl->inputName  = impl->session.GetInputNameAllocated(0, alloc).get();
		impl->outputName = impl->session.GetOutputNameAllocated(0, alloc).get();

		impl->worker = std::thread([p = impl.get()] { p->run(); });
		return std::unique_ptr<HostDetector>(new HostDetector(std::move(impl)));
	} catch (const std::exception& e) {
		error = e.what();
		return nullptr;
	}
}

HostDetector::HostDetector(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

HostDetector::~HostDetector() {
	if (!impl_) return;
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		impl_->stop = true;
	}
	impl_->cv.notify_all();
	if (impl_->worker.joinable()) impl_->worker.join();
}

bool HostDetector::submit(const uint8_t* rgb, int width, int height) {
	Impl& s = *impl_;
	if (s.busy.exchange(true)) return false;

	s.srcW = width;
	s.srcH = height;
	s.scale = std::min(float(kInputSize) / width, float(kInputSize) / height);
	const int contentW = int(width * s.scale + 0.5f), contentH = int(height * s.scale + 0.5f);
	const int offX = (kInputSize - contentW) / 2, offY = (kInputSize - contentH) / 2;
	s.padX = float(offX);
	s.padY = float(offY);

	// Letterbox with nearest-neighbour sampling; grey padding like the Ultralytics default
	std::fill(s.frame.begin(), s.frame.end(), uint8_t(114));
	for (int y = 0; y < contentH; ++y) {
		const int sy = std::min(height - 1, int((y + 0.5f) / s.scale));
		const uint8_t* srcRow = rgb + size_t(sy) * width * 3;
		uint8_t* dst = s.frame.data() + (size_t(y + offY) * kInputSize + offX) * 3;
		for (int x = 0; x < contentW; ++x) {
			const int sx = std::min(width - 1, int((x + 0.5f) / s.scale));
			dst[x * 3 + 0] = srcRow[sx * 3 + 0];
			dst[x * 3 + 1] = srcRow[sx * 3 + 1];
			dst[x * 3 + 2] = srcRow[sx * 3 + 2];
		}
	}

	{
		std::lock_guard<std::mutex> lock(s.mutex);
		s.pending = true;
	}
	s.cv.notify_one();
	return true;
}

bool HostDetector::fetch(std::vector<oak::Detection>& out) {
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (!impl_->resultFresh) return false;
	out = impl_->result;
	impl_->resultFresh = false;
	return true;
}

float HostDetector::inferMs() const { return impl_->inferMs; }
float HostDetector::fps() const { return impl_->fps; }

} // namespace detect
