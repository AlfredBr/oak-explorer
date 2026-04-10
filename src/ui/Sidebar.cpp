// src/ui/Sidebar.cpp
#include "ui/Sidebar.h"
#include "oak/OakStreams.h"
#include <imgui.h>

namespace ui {

void renderSidebar(const oak::OakDevice& device,
                   const oak::OakStreams& streams,
                   SpatialMode& mode) {
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

        ImGui::TextDisabled("State");
        if (streams.isStreaming()) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Streaming");
            ImGui::Spacing();

            ImGui::TextDisabled("Dropped frames");
            ImGui::Text("RGB: %d", streams.droppedRgbFrames());
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                info.available ? "Unbooted (ready)" : "Booted / in use");
            if (!info.available) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "(claimed by a process)");
            }
        }
        ImGui::Spacing();

        // --- Spatial mode + readout (only meaningful when streaming) ---
        if (streams.isStreaming()) {
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextDisabled("Spatial mode");
            bool lockSelected  = (mode == SpatialMode::Lock);
            bool trackSelected = (mode == SpatialMode::Track);
            if (ImGui::RadioButton("Lock",  lockSelected))  mode = SpatialMode::Lock;
            if (ImGui::RadioButton("Track", trackSelected)) mode = SpatialMode::Track;
            ImGui::Spacing();

            ImGui::TextDisabled("Sample point");
            if (streams.hasSpatial()) {
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f),
                    "X  %+.2f m", streams.spatialX());
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
                    "Y  %+.2f m", streams.spatialY());
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                    "Z  %+.2f m", streams.spatialZ());
            } else {
                ImGui::TextDisabled("Click Camera to sample");
            }
            ImGui::Spacing();
        }

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
