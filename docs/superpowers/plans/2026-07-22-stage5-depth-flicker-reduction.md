# Stage 5 — Depth Flicker Reduction — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate depth image flickering through incremental, safe filter steps. Each step is tested and committed independently.

**Architecture:** Step 1 uses on-device VPU filters (zero host changes). Steps 2–3 add minimal host-side post-processing on the existing depth buffer.

**Tech Stack:** depthai-core 2.x StereoDepth post-processing API, C++17, existing OakStreams class.

---

## Context for implementers

- Working directory: `E:\luxonis\oak-explorer\`
- Build: `cmake --build out/build/debug --config Debug --target oak-explorer`
- Run: `out\build\debug\Debug\oak-explorer.exe`
- Device must be plugged in via USB3 for runtime testing
- **No unit test framework.** Verification = build succeeds (0 errors) + visual confirmation that depth image is stable
- Previous attempt at CUDA-based filtering (Stage 5 old) caused crashes — this plan avoids new dependencies

---

## File Map

| File | Action | What changes |
|---|---|---|
| `src/oak/OakStreams.cpp` | Modify | Step 1: Add on-device filter config. Steps 2–3: Host-side post-processing in `poll()` |
| `src/oak/OakStreams.h` | Modify (Steps 2–3 only) | Add temporal history buffer member |

---

## Task 1: On-device StereoDepth filters ✅

**Files:**
- Modify: `src/oak/OakStreams.cpp`

- [x] **Step 1: Enable Left-Right check**
  Add `stereo->setLeftRightCheck(true)` after `setDefaultProfilePreset()`.

- [x] **Step 2: Enable Median 7×7 filter**
  Add `stereo->initialConfig.setMedianFilter(dai::MedianFilter::KERNEL_7x7)`.

- [x] **Step 3: Enable Speckle and Temporal filters via raw config**
  Use `initialConfig.get()` / `.set()` pattern to enable `speckleFilter` (range=50) and `temporalFilter`.

- [x] **Step 4: Build and visual test**
  Verify build succeeds. Run app and confirm depth image is noticeably more stable.

---

## Task 2: Host-side temporal EMA (Planned)

**Files:**
- Modify: `src/oak/OakStreams.h` — add `std::vector<float> depthEma_` member
- Modify: `src/oak/OakStreams.cpp` — blend current depth frame with EMA history in `poll()`

- [ ] **Step 1: Add EMA buffer**
  Add `std::vector<float> depthEma_` (640×400) to `OakStreams`, allocate in `open()`, clear in `close()`.

- [ ] **Step 2: Blend in poll()**
  After copying raw depth to `depthBuf_`, blend each pixel: `ema[i] = alpha * raw[i] + (1-alpha) * ema[i]`.
  Use the EMA result for the Turbo colormap instead of the raw value.
  Skip blending for invalid (zero) pixels.

- [ ] **Step 3: Build and visual test**

---

## Task 3: Fill invalid pixels (Planned)

**Files:**
- Modify: `src/oak/OakStreams.cpp` — add nearest-neighbor fill pass in `poll()` before colormap

- [ ] **Step 1: Add fill pass**
  After EMA blending (or raw copy if Task 2 skipped), scan for zero pixels and replace with nearest valid neighbor in a small radius.

- [ ] **Step 2: Build and visual test**
