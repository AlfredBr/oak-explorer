#include "ui/StereoView.h"
#include <imgui.h>
#include <algorithm>

namespace ui {
void renderStereoView(const oak::OakStreams& streams) {
    ImGui::SetNextWindowSize(ImVec2(900, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Stereo Cameras")) {
        static int mode = 0;
        static float blend = 0.5f;
        ImGui::RadioButton("Separate", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Overlay", &mode, 1);
        if (mode == 1) {
            ImGui::SetNextItemWidth(220);
            ImGui::SliderFloat("Right image blend", &blend, 0.0f, 1.0f, "%.2f");
        }
        ImGui::TextDisabled("Raw grayscale cameras (left / right)");
        if (!streams.isStreaming()) {
            ImGui::TextDisabled("No stream. Device not connected or pipeline failed.");
        } else if (!streams.leftTextureId() || !streams.rightTextureId()) {
            ImGui::TextDisabled("Waiting for a matching stereo pair...");
        } else {
            ImGui::TextUnformatted(mode == 0 ? "Left / Right" : "Left + Right");
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float gap = mode == 0 ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
            const float columns = mode == 0 ? 2.0f : 1.0f;
            const float aspect = float(streams.stereoWidth()) / streams.stereoHeight();
            const float width = std::min((available.x - gap) / columns,
                                         available.y * aspect);
            if (width > 0.0f) {
                const ImVec2 size(width, width / aspect);
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                auto* draw = ImGui::GetWindowDrawList();
                draw->AddImage((ImTextureID)(uintptr_t)streams.leftTextureId(),
                               origin, ImVec2(origin.x + size.x, origin.y + size.y));
                const float rightX = mode == 0 ? origin.x + size.x + gap : origin.x;
                draw->AddImage((ImTextureID)(uintptr_t)streams.rightTextureId(),
                               ImVec2(rightX, origin.y),
                               ImVec2(rightX + size.x, origin.y + size.y),
                               ImVec2(0, 0), ImVec2(1, 1),
                               IM_COL32(255, 255, 255, mode == 0 ? 255 : int(blend * 255)));
                ImGui::Dummy(ImVec2(size.x * columns + gap, size.y));
            }
        }
    }
    ImGui::End();
}
}
