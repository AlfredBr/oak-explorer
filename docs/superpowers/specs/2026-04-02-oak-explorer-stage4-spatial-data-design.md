# Stage 4 — Spatial Data: Click to Measure — Design Spec

**Date:** 2026-04-02
**Status:** Approved
**Project dir:** `E:\luxonis\oak-explorer\`

---

## Overview

Add click-to-measure spatial sampling to oak-explorer. The user clicks (or hovers) anywhere in the Camera window and the sidebar shows the real-world X, Y, Z position of that point in metres. A corner-bracket reticle drawn on the Camera image marks the current sample point.

**Milestone:** Click any pixel in the RGB view — get X, Y, Z in metres. Verify Z against a physical ruler at a known distance. The numbers match.

---

## Decisions Made

| Question | Decision |
|---|---|
| XYZ readout location | Sidebar — colour-coded, below streaming state |
| Which window is clickable | Camera only |
| Interaction model | Two modes: Lock (click to fix point) and Track (hover follows mouse), switchable from sidebar |
| ROI marker style | Corner brackets + centre dot, drawn via ImGui DrawList |
| Implementation approach | `SpatialLocationCalculator` + `XLinkIn` (the correct depthai pattern) |

---

## Architecture

### New depthai nodes (added to existing pipeline in `OakStreams`)

| Node | Purpose |
|---|---|
| `SpatialLocationCalculator` | Wired to StereoDepth output. Takes an ROI config, averages depth in that region, returns XYZ in mm using the camera's built-in intrinsics |
| `XLinkIn` ("spatialConfig") | Receives `SpatialLocationsConfig` packets from host when the ROI changes |
| `XLinkOut` ("spatial") | Sends `SpatialLocationData` packets back to host |

### New public interface on `OakStreams`

```cpp
// Send an updated ROI to the SpatialLocationCalculator.
// normX, normY are in [0,1] relative to the Camera image (1280×720).
// ROI size is fixed at 5%×5% of the frame.
void setRoi(float normX, float normY);

bool  hasSpatial() const;      // true once first result arrives
float spatialX()   const;      // metres, right of centre (positive = right)
float spatialY()   const;      // metres, down from centre (positive = down)
float spatialZ()   const;      // metres, forward (distance to subject)
ImVec2 spatialRoiCenter() const; // normalised ROI centre for StreamView to draw
```

### Interaction mode

`SpatialMode` enum lives in `main.cpp`:
```cpp
enum class SpatialMode { Lock, Track };
SpatialMode spatialMode = SpatialMode::Lock;
```

Passed by reference to `renderSidebar()` (displays toggle, user can change it) and `renderStreamView()` (reads it to decide click vs hover behaviour). The UI layer never touches depthai — it only calls `streams.setRoi()`.

### ROI size

Fixed at **5% × 5% of the frame** in normalised coords:
- In depth image terms (640×400): ~32×20 pixels
- Large enough to average out stereo noise, small enough to feel like a point sample
- Centred on the clicked/hovered pixel

---

## Data Flow

```
User clicks or hovers in Camera window (1280×720 px)
    ↓
StreamView maps pixel to normalised coords: (px/1280, py/720)
    ↓
streams.setRoi(normX, normY)
    constructs SpatialLocationsConfig with ROI = [normX±0.025, normY±0.025]
    ↓
OakStreams sends config packet via XLinkIn "spatialConfig" queue
    ↓  [USB — host → device]
SpatialLocationCalculator on OAK:
    averages depth values in ROI from StereoDepth
    computes XYZ using built-in camera intrinsics
    ↓  [USB — device → host]
OakStreams::poll() drains "spatial" XLinkOut via tryGet<SpatialLocationData>()
    result.spatialCoordinates: {x, y, z} in mm → divide by 1000 → metres
    ↓
Sidebar reads streams.spatialX/Y/Z() and displays them
```

---

## OAK Coordinate System

Origin is at the left stereo camera lens. Axes:

| Axis | Direction | Positive means |
|---|---|---|
| Z | Forward from camera | Subject is farther away — verify with ruler |
| X | Right | Subject is to the right of centre |
| Y | Down | Subject is below centre |

---

## UI Design

### Sidebar additions

Below the existing "Streaming" / dropped frames section:

```
─────────────────────
Spatial mode
( ) Lock   (•) Track       ← radio buttons

Sample point
X  +0.32 m                 ← blue
Y  -0.15 m                 ← green
Z   1.84 m                 ← orange
─────────────────────
```

"Click Camera to sample" hint shown when `hasSpatial()` is false.

### Camera window overlay (StreamView)

Corner-bracket reticle drawn via `ImGui::GetWindowDrawList()` at the ROI centre:
- Four L-shaped corners (bracket length ~8px, line weight 1.5px), white with 80% alpha
- 3px centre dot
- Reticle tracks mouse in Track mode; stays fixed in Lock mode

---

## File Map

| File | Action |
|---|---|
| `src/oak/OakStreams.h` | Add spatial interface: `setRoi()`, `hasSpatial()`, `spatialX/Y/Z()`, `spatialRoiCenter()` |
| `src/oak/OakStreams.cpp` | Add `SpatialLocationCalculator` + `XLinkIn` to pipeline; implement `setRoi()`; drain spatial queue in `poll()` |
| `src/ui/StreamView.h` | Add `SpatialMode` parameter to `renderStreamView()` |
| `src/ui/StreamView.cpp` | Draw corner brackets via ImDrawList; handle mouse click/hover; call `streams.setRoi()` |
| `src/ui/Sidebar.h` | Add `SpatialMode&` parameter to `renderSidebar()` |
| `src/ui/Sidebar.cpp` | Add mode radio buttons + colour-coded XYZ section |
| `src/main.cpp` | Add `SpatialMode` local var; pass to both render functions |

---

## What You'll Learn

- `SpatialLocationCalculator` node — the OAK's built-in ROI depth averager
- `XLinkIn` — host→device data (config packets), the reverse of the usual flow
- `SpatialLocations` packet format and the `spatialCoordinates` struct
- OAK coordinate system (X right, Y down, Z forward)
- How ImGui maps mouse position to image pixel space (`GetMousePos()` vs window origin)
- How the pinhole camera model underlies the XYZ math (background — OAK does the actual computation)

---

## Out of Scope

- Depth-window clicking (different sensor perspective, different coordinate transform)
- Adjustable ROI size (fixed 5%×5% is sufficient for learning)
- History / trail of past sample points
- Distance between two clicked points
