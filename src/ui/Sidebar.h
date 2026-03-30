// src/ui/Sidebar.h
#pragma once
#include "oak/Device.h"
#include "oak/CameraStream.h"

namespace ui {

// Renders the left sidebar. Call inside an active ImGui frame.
void renderSidebar(const oak::OakDevice& device, const oak::CameraStream& stream);

} // namespace ui
