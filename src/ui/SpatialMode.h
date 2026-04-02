// src/ui/SpatialMode.h
#pragma once

namespace ui {

// Controls how the Camera window samples a spatial point.
// Lock  — click to fix the ROI; it stays until the next click.
// Track — ROI follows the mouse continuously while hovering.
enum class SpatialMode { Lock, Track };

} // namespace ui
