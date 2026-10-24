// src/ui/StreamView.h
#pragma once
#include "oak/OakStreams.h"
#include "ui/SpatialMode.h"
#include "ui/DetectionSettings.h"

namespace ui {

// Renders the Camera ImGui window (RGB stream).
// Handles mouse input for spatial ROI sampling.
// streams is non-const — calls streams.setRoi() on click/hover.
// Call inside an active ImGui frame, after streams.poll().
void renderStreamView(oak::OakStreams& streams, SpatialMode mode, const DetectionSettings& det);

} // namespace ui
