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

            // --- ImPlot histogram ---
            // Build vector of valid (non-zero) depth values for this frame
            const uint16_t* raw    = streams.rawDepthData();  // valid until next poll()
            const int       count  = streams.depthPixelCount();

            std::vector<float> validDepths;
            validDepths.reserve(count);
            for (int i = 0; i < count; ++i) {
                if (raw[i] > 0) {
                    validDepths.push_back((float)raw[i]);
                }
            }

            if (ImPlot::BeginPlot("Depth Distribution", ImVec2(-1, 160))) {
                ImPlot::SetupAxes("Depth (mm)", "Pixels");
                ImPlot::SetupAxesLimits(400.0, 10000.0, 0.0, 1.0, ImPlotCond_Once);
                if (!validDepths.empty()) {
                    ImPlot::PlotHistogram("##depth",
                        validDepths.data(),
                        (int)validDepths.size(),
                        100,                // 100 bins
                        1.0,                // bar scale
                        ImPlotRange(400.0, 10000.0));
                }
                ImPlot::EndPlot();
            }

        } else {
            ImGui::Spacing();
            ImGui::TextDisabled("No stream");
            ImGui::TextDisabled("Device not connected or pipeline failed.");
        }
    }
    ImGui::End();
}

} // namespace ui
