/**
 * @file stats_window.cpp
 * The stats window: a card with a header, a thin line under it, and a two-column table of dim labels and their values.
 */
#include "ui/stats_window.hpp"

#include "ui/card.hpp"

#include <imgui.h>

namespace
{
    /** The table's columns: the label and its value. */
    constexpr int ColumnCount = 2;

    /** Milliseconds in a second. */
    constexpr float MillisecondsPerSecond = 1000.0f;

    /** Starts a table row with its label, dim, so the values stand out; the value goes into the next column. */
    void BeginRow(const char* label)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", label);
        ImGui::TableNextColumn();
    }
} // namespace

void Wotah::ShowStatsWindow(const FrameStats& stats)
{
    // BeginTable says false while the table is out of sight; EndTable is due only after a true (imgui.h, BeginTable).
    if (BeginCard(StatsWindowTitle, StatsWindowPosition, TopLeftPivot))
    {
        ImGui::TextUnformatted(StatsWindowTitle);
        ImGui::Separator();

        if (ImGui::BeginTable("values", ColumnCount))
        {
            // ImGui's frame rate is an average, so the numbers stay readable instead of flickering every frame.
            const float framesPerSecond = ImGui::GetIO().Framerate;
            const float frameTime       = framesPerSecond > 0.0f ? MillisecondsPerSecond / framesPerSecond : 0.0f;

            BeginRow("Frame rate");
            ImGui::Text("%.0f fps", static_cast<double>(framesPerSecond));
            BeginRow("Frame time");
            ImGui::Text("%.2f ms", static_cast<double>(frameTime));
            BeginRow("Picture");
            ImGui::Text("%u x %u", stats.PictureWidth, stats.PictureHeight);
            BeginRow("Camera");
            ImGui::Text("%.2f, %.2f, %.2f", static_cast<double>(stats.CameraPosition.x),
                        static_cast<double>(stats.CameraPosition.y), static_cast<double>(stats.CameraPosition.z));
            ImGui::EndTable();
        }
    }
    EndCard();
}
