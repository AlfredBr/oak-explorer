# Stage 5 — Depth Flicker Reduction — Design Spec

**Date:** 2026-07-22
**Status:** In Progress
**Project dir:** `E:\luxonis\oak-explorer\`

---

## Overview

The raw stereo depth image flickers badly — noisy frame-to-frame variation, salt-and-pepper speckle, and black holes where stereo matching fails. This stage applies filters incrementally to make the depth image visually stable and usable without crashing or destabilizing the application.

**Milestone:** Depth heatmap is visually stable. Speckles are reduced. Frame-to-frame flicker is eliminated. No changes to buffer layouts, data flow, or host-side processing (Step 1).

**Verified:** Steps 1+2 confirmed working — depth image is dramatically more stable with clear silhouettes, smooth color gradients, and minimal flicker at 59.6 FPS. Some remaining black/cyan spots at occlusion edges (Step 3 target).

---

## Decisions Made

| Question | Decision |
|---|---|
| Filter location (Step 1) | On-device (VPU) — StereoDepth node pipeline config only; zero host-side changes |
| Approach | Incremental steps; each step is tested and committed before the next |
| Previous CUDA plan (Stage 5 old) | Deferred — on-device filters are tried first since they are safer and require no new dependencies |
| Median kernel size | 7×7 — strongest spatial smoothing without decimation |
| Temporal filter defaults | Alpha = 0.4, persistency = VALID_2_IN_LAST_4 (SDK defaults) |

---

## Incremental Plan

| Step | What | Risk | Status |
|------|------|------|--------|
| **1** | Enable on-device StereoDepth filters (LR-check, median, speckle, temporal) | Very low — pipeline config only | ✅ Done |
| **2** | Host-side temporal EMA blending of raw depth buffer | Low — adds one extra buffer | ✅ Done |
| **3** | Fill invalid (zero) pixels with nearest-neighbor interpolation | Low — post-process on existing buffer | Planned |

---

## Step 1 — On-Device Filters (Completed)

### What Changed

Four on-device filters enabled in `OakStreams::open()` on the `StereoDepth` pipeline node:

| Filter | API | Effect |
|--------|-----|--------|
| Left-Right Check | `stereo->setLeftRightCheck(true)` | Computes disparity in both L→R and R→L directions; discards pixels where they disagree. Removes phantom depth at occlusion edges. |
| Median 7×7 | `stereo->initialConfig.setMedianFilter(dai::MedianFilter::KERNEL_7x7)` | Replaces each pixel with the median of its 7×7 neighborhood. Removes salt-and-pepper noise while preserving edges. |
| Speckle Filter | `cfg.postProcessing.speckleFilter.enable = true` | Removes small isolated noise patches. `speckleRange = 50`. |
| Temporal Filter | `cfg.postProcessing.temporalFilter.enable = true` | On-device EMA blending across consecutive frames. Primary flicker reducer. Uses SDK defaults (alpha=0.4, VALID_2_IN_LAST_4). |

### Why This Is Safe

- All filters run entirely on the OAK device's VPU
- No host-side buffer sizes, data layouts, or processing loops changed
- Output remains `uint16` depth in mm at 640×400 — identical format
- The `get()`/`set()` pattern on `initialConfig` is the documented way to access post-processing filters in the DepthAI v2 SDK

### Files Modified

| File | Change |
|------|--------|
| `src/oak/OakStreams.cpp` | Added filter configuration block (lines 114–126) in `open()` after `setDefaultProfilePreset()` |

---

## Step 2 — Host-Side Temporal EMA (Completed)

### What Changed

Added a per-pixel exponential moving average (EMA) buffer that blends each new depth frame with the running history before applying the Turbo colormap.

| Component | Detail |
|-----------|--------|
| New member | `std::vector<float> depthEma_` (640×400) in `OakStreams` |
| Alpha | 0.3 — moderate smoothing (lower = smoother but laggier) |
| Invalid pixels | Zero-value pixels don't corrupt the EMA; the previous smoothed value persists |
| Seeding | First valid sample for each pixel seeds the EMA directly (no blend) |
| Colormap source | Changed from raw `depthData[i]` to `ema[i]` for the Turbo LUT lookup |

### Why This Is Safe

- Adds one `float` buffer (640×400×4 = ~1 MB) — negligible memory
- Same single-threaded `poll()` loop — no concurrency changes
- Raw `depthBuf_` still copied from the device frame unchanged — histogram is unaffected
- The EMA loop and colormap loop are separate passes over the same pixel count

### Files Modified

| File | Change |
|------|--------|
| `src/oak/OakStreams.h` | Added `depthEma_` member |
| `src/oak/OakStreams.cpp` | Allocate in `open()`, clear in `close()`, EMA blend + colormap in `poll()` |
