// src/ui/StreamView.h
#pragma once
#include "oak/OakStreams.h"

namespace ui {

// Renders the Camera ImGui window (RGB stream).
// Call inside an active ImGui frame, after streams.poll().
void renderStreamView(const oak::OakStreams& streams);

} // namespace ui
