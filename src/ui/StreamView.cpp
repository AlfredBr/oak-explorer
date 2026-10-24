// src/ui/StreamView.cpp
#include "ui/StreamView.h"
#include <imgui.h>
#include <algorithm>  // std::clamp
#include <cmath>
#include <cstdio>

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

static void drawDetections(ImDrawList* dl, const oak::OakStreams& streams,
                           const DetectionSettings& det, ImVec2 imgMin, ImVec2 imgMax) {
    const float w = imgMax.x - imgMin.x, h = imgMax.y - imgMin.y;
    dl->PushClipRect(imgMin, imgMax, true);
    for (const auto& d : streams.detections()) {
        if (d.confidence < det.minConfidence) continue;
        const ImVec2 p0(imgMin.x + d.xmin * w, imgMin.y + d.ymin * h);
        const ImVec2 p1(imgMin.x + d.xmax * w, imgMin.y + d.ymax * h);
        // Colour per class so different objects are easy to tell apart
        const ImU32 col = ImColor::HSV(std::fmod(d.label * 0.137f, 1.0f), 0.8f, 1.0f);
        dl->AddRect(p0, p1, col, 0.0f, 0, 2.0f);

        char text[64];
        snprintf(text, sizeof(text), "%s %d%%", oak::detectionLabelName(d.label),
                 (int)(d.confidence * 100.0f + 0.5f));
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddRectFilled(ImVec2(p0.x, p0.y - ts.y - 4), ImVec2(p0.x + ts.x + 6, p0.y), col);
        dl->AddText(ImVec2(p0.x + 3, p0.y - ts.y - 2), IM_COL32(0, 0, 0, 255), text);
    }
    dl->PopClipRect();
}

void renderStreamView(oak::OakStreams& streams, SpatialMode mode, const DetectionSettings& det) {
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

        if (det.show && streams.hasDetector())
            drawDetections(ImGui::GetWindowDrawList(), streams, det, imageMin, imageMax);

        // --- Corner-bracket reticle
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
