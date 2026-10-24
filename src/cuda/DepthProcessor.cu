// src/cuda/DepthProcessor.cu
#include "cuda/DepthProcessor.h"

#include <cuda_runtime.h>
#include <cstdio>

namespace cuda {

namespace {

__constant__ uint8_t kLut[256][3];

constexpr float kMinMm = 400.0f;
constexpr float kMaxMm = 10000.0f;

// Same semantics as the CPU loop: invalid input keeps history, the first valid
// sample seeds the pixel, later samples are blended.
__global__ void emaKernel(const uint16_t* __restrict__ in, float* __restrict__ ema,
						  int n, float alpha)
{
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n) return;
	float raw = (float)in[i];
	if (raw == 0.0f) return;
	float prev = ema[i];
	ema[i] = (prev == 0.0f) ? raw : alpha * raw + (1.0f - alpha) * prev;
}

// 3x3 fill of pixels that have never held a valid value. Reads src, writes dst
// so the result does not depend on thread order.
__global__ void holeFillKernel(const float* __restrict__ src, float* __restrict__ dst,
							   int w, int h)
{
	int x = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;
	if (x >= w || y >= h) return;

	int idx = y * w + x;
	float v = src[idx];
	if (v == 0.0f) {
		float sum = 0.0f;
		int cnt = 0;
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				int nx = x + dx, ny = y + dy;
				if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
				float nv = src[ny * w + nx];
				if (nv != 0.0f) { sum += nv; ++cnt; }
			}
		}
		if (cnt > 0) v = sum / (float)cnt;
	}
	dst[idx] = v;
}

__global__ void colormapKernel(const float* __restrict__ src, uint8_t* __restrict__ rgb, int n)
{
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n) return;
	float d = src[i];
	uint8_t r = 0, g = 0, b = 0;
	if (d != 0.0f) {
		int idx = (int)((kMaxMm - d) / (kMaxMm - kMinMm) * 255.0f);
		idx = idx < 0 ? 0 : (idx > 255 ? 255 : idx);
		r = kLut[idx][0]; g = kLut[idx][1]; b = kLut[idx][2];
	}
	rgb[i * 3]     = r;
	rgb[i * 3 + 1] = g;
	rgb[i * 3 + 2] = b;
}

bool check(cudaError_t e, const char* what) {
	if (e == cudaSuccess) return true;
	fprintf(stderr, "DepthProcessor: %s failed: %s\n", what, cudaGetErrorString(e));
	return false;
}

} // namespace

bool DepthProcessor::available() {
	int count = 0;
	return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

bool DepthProcessor::init(int width, int height, const uint8_t* turboLut,
						  uint16_t* hostDepth, uint8_t* hostRgb) {
	destroy();
	if (width <= 0 || height <= 0 || !hostDepth || !hostRgb) return false;
	width_ = width;
	height_ = height;
	const size_t n = (size_t)width * height;

	cudaStream_t s = nullptr;
	bool ok = check(cudaStreamCreate(&s), "cudaStreamCreate");
	stream_ = s;
	ok = ok && check(cudaMemcpyToSymbol(kLut, turboLut, 256 * 3), "upload LUT");
	ok = ok && check(cudaMalloc(&d_input_, n * sizeof(uint16_t)), "alloc input");
	ok = ok && check(cudaMalloc(&d_emaA_, n * sizeof(float)), "alloc emaA");
	ok = ok && check(cudaMalloc(&d_emaB_, n * sizeof(float)), "alloc emaB");
	ok = ok && check(cudaMalloc(&d_rgb_, n * 3), "alloc rgb");
	ok = ok && check(cudaMemset(d_emaA_, 0, n * sizeof(float)), "clear emaA");
	ok = ok && check(cudaMemset(d_emaB_, 0, n * sizeof(float)), "clear emaB");
	if (!ok) {
		destroy();
		return false;
	}

	hostDepth_ = hostDepth;
	hostRgb_   = hostRgb;
	// Page-locked memory lets the copies use DMA directly. Failure is non-fatal:
	// transfers still work from pageable memory, just more slowly.
	pinnedDepth_ = cudaHostRegister(hostDepth, n * sizeof(uint16_t), cudaHostRegisterDefault) == cudaSuccess;
	pinnedRgb_   = cudaHostRegister(hostRgb, n * 3, cudaHostRegisterDefault) == cudaSuccess;
	cudaGetLastError();  // clear any sticky error from a failed register

	initialised_ = true;
	return true;
}

bool DepthProcessor::process(float emaAlpha) {
	if (!initialised_) return false;
	const uint16_t* hostDepth = hostDepth_;
	uint8_t* hostRgb = hostRgb_;
	const int n = width_ * height_;
	cudaStream_t s = (cudaStream_t)stream_;

	const dim3 block1(256);
	const dim3 grid1((n + block1.x - 1) / block1.x);
	const dim3 block2(16, 16);
	const dim3 grid2((width_ + 15) / 16, (height_ + 15) / 16);

	bool ok = check(cudaMemcpyAsync(d_input_, hostDepth, (size_t)n * sizeof(uint16_t),
									cudaMemcpyHostToDevice, s), "upload depth");
	if (ok) {
		emaKernel<<<grid1, block1, 0, s>>>(d_input_, d_emaA_, n, emaAlpha);
		holeFillKernel<<<grid2, block2, 0, s>>>(d_emaA_, d_emaB_, width_, height_);
		// The filled result becomes the next frame's history, as on the CPU path.
		float* t = d_emaA_; d_emaA_ = d_emaB_; d_emaB_ = t;
		colormapKernel<<<grid1, block1, 0, s>>>(d_emaA_, d_rgb_, n);
		ok = check(cudaGetLastError(), "kernel launch");
	}
	ok = ok && check(cudaMemcpyAsync(hostRgb, d_rgb_, (size_t)n * 3,
									 cudaMemcpyDeviceToHost, s), "download rgb");
	ok = ok && check(cudaStreamSynchronize(s), "stream sync");
	return ok;
}

void DepthProcessor::destroy() {
	if (pinnedDepth_) cudaHostUnregister(hostDepth_);
	if (pinnedRgb_)   cudaHostUnregister(hostRgb_);
	pinnedDepth_ = pinnedRgb_ = false;
	hostDepth_ = nullptr;
	hostRgb_ = nullptr;
	cudaFree(d_input_); d_input_ = nullptr;
	cudaFree(d_emaA_);  d_emaA_  = nullptr;
	cudaFree(d_emaB_);  d_emaB_  = nullptr;
	cudaFree(d_rgb_);   d_rgb_   = nullptr;
	if (stream_) { cudaStreamDestroy((cudaStream_t)stream_); stream_ = nullptr; }
	initialised_ = false;
	width_ = height_ = 0;
}

} // namespace cuda
