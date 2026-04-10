// src/ui/DepthView.cpp
#include "ui/DepthView.h"
#include <imgui.h>
#include <implot.h>

#include <vector>

namespace ui {

void renderDepthView(const oak::OakStreams& streams) {
    // Default position on first run (before imgui.ini is saved)
    ImGui::SetNextWindowPos(ImVec2(240, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900, 540), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Depth")) {
        if (streams.isStreaming()) {
            // --- Heatmap image (aspect-fit to window width) ---
            float avail  = ImGui::GetContentRegionAvail().x;
            float aspect = (float)streams.depthHeight() / (float)streams.depthWidth();
            ImGui::Image(
                (ImTextureID)(uintptr_t)streams.depthTextureId(),
                ImVec2(avail, avail * aspect)
            );
        } else {
            ImGui::Spacing();
            ImGui::TextDisabled("No stream");
            ImGui::TextDisabled("Device not connected or pipeline failed.");
        }
    }
    ImGui::End();

    // --- Depth Distribution — separate resizable window ---
    ImGui::SetNextWindowPos(ImVec2(240, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Depth Distribution")) {
        if (streams.isStreaming()) {
            const uint16_t* raw   = streams.rawDepthData();
            const int       count = streams.depthPixelCount();

            std::vector<float> validDepths;
            validDepths.reserve(count);
            for (int i = 0; i < count; ++i) {
                if (raw[i] > 0) {
                    validDepths.push_back((float)raw[i]);
                }
            }

            // Fill the entire window content area
            ImVec2 plotSize = ImGui::GetContentRegionAvail();
            if (ImPlot::BeginPlot("##depth_dist", plotSize)) {
                ImPlot::SetupAxes("Depth (mm)", "Pixels");
                ImPlot::SetupAxisLimits(ImAxis_X1, 400.0, 10000.0, ImPlotCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0, ImPlotCond_Once);
                ImPlot::SetupAxisZoomConstraints(ImAxis_Y1, 1.0, HUGE_VAL);  // prevent Y min going below 0
                if (!validDepths.empty()) {
                    ImPlot::PlotHistogram("##depth",
                        validDepths.data(),
                        (int)validDepths.size(),
                        100,
                        1.0,
                        ImPlotRange(400.0, 10000.0));
                }
                ImPlot::EndPlot();
            }
        } else {
            ImGui::TextDisabled("No stream");
        }
    }
    ImGui::End();
}

} // namespace ui
