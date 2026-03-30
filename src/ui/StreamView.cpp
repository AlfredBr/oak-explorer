// src/ui/StreamView.cpp
#include "ui/StreamView.h"
#include <imgui.h>

namespace ui {

void renderStreamView(const oak::CameraStream& stream) {
    ImGui::Begin("Camera");

    if (stream.isStreaming()) {
        // Scale image to fill available window width, preserve aspect ratio
        float avail  = ImGui::GetContentRegionAvail().x;
        float aspect = (float)stream.height() / (float)stream.width();
        ImGui::Image(
            (ImTextureID)(uintptr_t)stream.textureId(),
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
