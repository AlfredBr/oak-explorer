// src/ui/StreamView.cpp
#include "ui/StreamView.h"
#include <imgui.h>
#include <algorithm>  // std::clamp

namespace ui {

// Draws a corner-bracket reticle centred at (cx, cy) in screen space.
static void drawReticle(ImDrawList* dl, float cx, float cy) {
    const float half = 12.0f;  // half-size of bracket box
    const float arm  =  7.0f;  // length of each bracket arm
    const float lw   =  1.5f;  // line width
    const ImU32 col  = IM_COL32(255, 255, 255, 210);

    // Top-left corner
    dl->AddLine(ImVec2(cx - half, cy - half + arm), ImVec2(cx - half, cy - half), col, lw);
    dl->AddLine(ImVec2(cx - half, cy - half),       ImVec2(cx - half + arm, cy - half), col, lw);
    // Top-right corner
    dl->AddLine(ImVec2(cx + half - arm, cy - half), ImVec2(cx + half, cy - half), col, lw);
    dl->AddLine(ImVec2(cx + half, cy - half),       ImVec2(cx + half, cy - half + arm), col, lw);
    // Bottom-left corner
    dl->AddLine(ImVec2(cx - half, cy + half - arm), ImVec2(cx - half, cy + half), col, lw);
    dl->AddLine(ImVec2(cx - half, cy + half),       ImVec2(cx - half + arm, cy + half), col, lw);
    // Bottom-right corner
    dl->AddLine(ImVec2(cx + half - arm, cy + half), ImVec2(cx + half, cy + half), col, lw);
    dl->AddLine(ImVec2(cx + half, cy + half),       ImVec2(cx + half, cy + half - arm), col, lw);
    // Centre dot
    dl->AddCircleFilled(ImVec2(cx, cy), 2.5f, col);
}

void renderStreamView(oak::OakStreams& streams, SpatialMode mode) {
    ImGui::SetNextWindowPos(ImVec2(240, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900, 540), ImGuiCond_FirstUseEver);
    ImGui::Begin("Camera");

    if (streams.isStreaming()) {
        float avail  = ImGui::GetContentRegionAvail().x;
        float aspect = (float)streams.rgbHeight() / (float)streams.rgbWidth();
        ImVec2 imageSize(avail, avail * aspect);

        ImGui::Image(
            (ImTextureID)(uintptr_t)streams.rgbTextureId(),
            imageSize
        );

        // Item rect is valid immediately after Image()
        ImVec2 imageMin = ImGui::GetItemRectMin();
        ImVec2 imageMax = ImGui::GetItemRectMax();
        float  imgW     = imageMax.x - imageMin.x;
        float  imgH     = imageMax.y - imageMin.y;

        // --- Mouse input ---
        if (ImGui::IsItemHovered()) {
            ImVec2 mouse = ImGui::GetMousePos();
            float normX = std::clamp((mouse.x - imageMin.x) / imgW, 0.0f, 1.0f);
            float normY = std::clamp((mouse.y - imageMin.y) / imgH, 0.0f, 1.0f);

            if (mode == SpatialMode::Track) {
                streams.setRoi(normX, normY);
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                streams.setRoi(normX, normY);
            }
        }

        // --- Corner-bracket reticle at current ROI centre ---
        float cx = imageMin.x + streams.roiCenterX() * imgW;
        float cy = imageMin.y + streams.roiCenterY() * imgH;
        drawReticle(ImGui::GetWindowDrawList(), cx, cy);

    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("No stream");
        ImGui::TextDisabled("Device not connected or pipeline failed.");
    }

    ImGui::End();
}

} // namespace ui
