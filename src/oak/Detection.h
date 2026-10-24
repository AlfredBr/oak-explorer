// src/oak/Detection.h
#pragma once

namespace oak {

// One object found by a neural network. Box corners are normalised [0,1]
// relative to the full RGB image (letterboxing already undone).
struct Detection {
	float xmin = 0, ymin = 0, xmax = 0, ymax = 0;
	float confidence = 0;
	int   label = -1;
};

// Lowest confidence the detectors report; the UI can only filter upwards from here.
constexpr float kMinDetectionConfidence = 0.3f;

// COCO class name for a label index, or "?" if out of range.
const char* detectionLabelName(int label);

} // namespace oak
