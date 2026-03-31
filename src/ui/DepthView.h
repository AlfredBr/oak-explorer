#pragma once
#include "oak/OakStreams.h"

namespace ui {

// Renders the Depth ImGui window: heatmap image above, ImPlot histogram below.
// Call inside an active ImGui frame, after streams.poll().
void renderDepthView(const oak::OakStreams& streams);

} // namespace ui
