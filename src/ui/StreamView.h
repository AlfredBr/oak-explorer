// src/ui/StreamView.h
#pragma once
#include "oak/CameraStream.h"

namespace ui {

// Renders the Camera ImGui window.
// Call inside an active ImGui frame, after stream.poll().
void renderStreamView(const oak::CameraStream& stream);

} // namespace ui
