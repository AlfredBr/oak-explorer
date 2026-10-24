// src/cuda/DepthProcessor.h
// GPU implementation of the host-side depth post-processing (temporal EMA,
// 3x3 hole fill, Turbo colormap). Deliberately free of CUDA and OpenGL headers
// so it can be included from ordinary C++ translation units.
#pragma once
#include <cstdint>

namespace cuda {

class DepthProcessor {
public:
	DepthProcessor() = default;
	~DepthProcessor() { destroy(); }
	DepthProcessor(const DepthProcessor&) = delete;
	DepthProcessor& operator=(const DepthProcessor&) = delete;

	// True if at least one CUDA device is usable. Never throws.
	static bool available();

	// Allocate device buffers for a width x height frame. Resets EMA history.
	// turboLut: 256 RGB entries (768 bytes), copied to the device.
	// hostDepth / hostRgb: the buffers later passed to process(). They are page-locked
	// for faster transfers and must stay allocated and unmoved until destroy().
	// Returns false (and logs to stderr) on failure; the object stays unusable.
	bool init(int width, int height, const uint8_t* turboLut,
			  uint16_t* hostDepth, uint8_t* hostRgb);

	// Filter one frame and write the colormapped result to hostRgb.
	// hostDepth: width*height uint16 mm values (0 = invalid) — the buffer given to init().
	// hostRgb:   width*height*3 bytes — the buffer given to init().
	// Returns false on a CUDA error; the caller should fall back to the CPU path.
	bool process(float emaAlpha);

	bool ready() const { return initialised_; }

	// Release device memory. Safe to call repeatedly.
	void destroy();

private:
	int width_  = 0;
	int height_ = 0;

	uint16_t* d_input_ = nullptr;  // raw frame
	float*    d_emaA_  = nullptr;  // persistent EMA history (current)
	float*    d_emaB_  = nullptr;  // hole-fill output (swapped with A each frame)
	uint8_t*  d_rgb_   = nullptr;  // colormapped output
	void*     stream_  = nullptr;  // cudaStream_t
	uint16_t* hostDepth_ = nullptr;  // caller-owned buffers given to init()
	uint8_t*  hostRgb_   = nullptr;
	bool pinnedDepth_ = false;       // cudaHostRegister succeeded
	bool pinnedRgb_   = false;

	bool initialised_ = false;
};

} // namespace cuda
