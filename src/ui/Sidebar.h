// src/ui/Sidebar.h
#pragma once
#include "oak/Device.h"
#include "oak/OakStreams.h"
#include "ui/SpatialMode.h"

namespace ui {

// Renders the left sidebar. Call inside an active ImGui frame.
// mode is read-write — the sidebar shows the toggle and the user can change it.
void renderSidebar(const oak::OakDevice& device,
                   const oak::OakStreams& streams,
                   SpatialMode& mode);

} // namespace ui
