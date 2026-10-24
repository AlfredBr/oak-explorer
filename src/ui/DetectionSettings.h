// src/ui/DetectionSettings.h
#pragma once
#include "oak/OakStreams.h"

namespace ui {

// User-adjustable detection display options, owned by main() and shared by the
// sidebar (edits) and the Camera view (reads).
struct DetectionSettings {
	bool  show          = true;
	float minConfidence = 0.5f;  // host-side filter; the detectors report from kMinDetectionConfidence up
	oak::DetectorSource source = oak::DetectorSource::Vpu;
};

// Hides camera pixels that are farther than a chosen distance.
struct DepthSquelchSettings {
	bool  enabled     = false;
	float maxMeters   = 3.0f;
	bool  hideUnknown = false;  // also hide pixels without a valid depth reading
	bool  limitDetection = true; // detection ignores anything beyond maxMeters
};

} // namespace ui
