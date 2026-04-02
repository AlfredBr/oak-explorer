// src/ui/Sidebar.cpp
#include "ui/Sidebar.h"
#include "oak/OakStreams.h"
#include <imgui.h>

namespace ui {

void renderSidebar(const oak::OakDevice& device, const oak::OakStreams& streams) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(220, ImGui::GetIO().DisplaySize.y), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Device", nullptr, flags);

    // Connection status indicator
    // Note: ImGui's default font (ProggyClean) is ASCII-only — use ImGui::Bullet()
    // instead of Unicode bullet characters like ●
    if (device.isConnected()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::Text("Connected");
        ImGui::PopStyleColor();

        const auto& info = device.info();
        ImGui::Spacing();

        ImGui::TextDisabled("MX ID");
        ImGui::TextWrapped("%s", info.mxId.c_str());
        ImGui::Spacing();

        // State: UNBOOTED = visible but no pipeline running (normal for Stage 1)
        //        BOOTED   = pipeline active (Stage 2+)
        ImGui::TextDisabled("State");
        if (streams.isStreaming()) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Streaming");
            ImGui::Spacing();

            ImGui::TextDisabled("Dropped frames");
            ImGui::Text("RGB:   %d", streams.droppedRgbFrames());
            ImGui::Text("Depth: %d", streams.droppedDepthFrames());
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                info.available ? "Unbooted (ready)" : "Booted / in use");
            if (!info.available) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "(claimed by a process)");
            }
        }
        ImGui::Spacing();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::Text("No device");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextDisabled("Connect OAK-D-Lite");
        ImGui::TextDisabled("via USB3 and relaunch.");
    }

    // Performance stats — pinned to the bottom of the sidebar
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 56.0f);
    ImGui::Separator();
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
#ifdef NDEBUG
    ImGui::TextDisabled("RELEASE");
#else
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "DEBUG");
#endif

    ImGui::End();
}

} // namespace ui
