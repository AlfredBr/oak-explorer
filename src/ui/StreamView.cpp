// src/ui/StreamView.cpp
#include "ui/StreamView.h"
#include <imgui.h>

namespace ui {

void renderStreamView(const oak::OakStreams& streams) {
    ImGui::Begin("Camera");

    if (streams.isStreaming()) {
        float avail  = ImGui::GetContentRegionAvail().x;
        float aspect = (float)streams.rgbHeight() / (float)streams.rgbWidth();
        ImGui::Image(
            (ImTextureID)(uintptr_t)streams.rgbTextureId(),
            ImVec2(avail, avail * aspect)
        );
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("No stream");
        ImGui::TextDisabled("Device not connected or pipeline failed.");
    }

    ImGui::End();
}

} // namespace ui
