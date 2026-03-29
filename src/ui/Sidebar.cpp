// src/ui/Sidebar.cpp
#include "ui/Sidebar.h"
#include <imgui.h>

namespace ui {

void renderSidebar(const oak::OakDevice& device) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(220, ImGui::GetIO().DisplaySize.y), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Device", nullptr, flags);

    // Connection status dot
    if (device.isConnected()) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "● Connected");
        const auto& info = device.info();
        ImGui::Spacing();
        ImGui::TextDisabled("MX ID");
        ImGui::TextWrapped("%s", info.mxId.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Device");
        ImGui::TextWrapped("%s", info.name.c_str());
        ImGui::Spacing();
        if (!info.available) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "(in use by another process)");
        }
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "● No device");
        ImGui::TextDisabled("Connect OAK-D-Lite");
        ImGui::TextDisabled("via USB3 and relaunch.");
    }

    ImGui::End();
}

} // namespace ui
