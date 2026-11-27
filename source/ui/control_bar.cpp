/**
 * @file control_bar.cpp
 * The control bar: a card at the bottom middle with four buttons in a row.
 */
#include "ui/control_bar.hpp"

#include "ui/card.hpp"

#include <glm/vec2.hpp>
#include <imgui.h>

#include <algorithm>

namespace
{
    /**
     * The buttons' labels. After "###" comes the name ImGui knows a button by, so the pause button stays one button
     * whichever label it shows (imgui.h, "About the ID Stack").
     */
    constexpr const char* AddBallLabel     = "Add ball";
    constexpr const char* ResetSceneLabel  = "Reset scene";
    constexpr const char* PauseLabel       = "Pause###pause";
    constexpr const char* ResumeLabel      = "Resume###pause";
    constexpr const char* ResetCameraLabel = "Reset camera";

    /** Colors pushed for the pause button while paused: its fill and its fill under the mouse. */
    constexpr int PausedColorCount = 2;

    /** The pause button's size: wide enough for the wider of its two labels, as high as ImGui makes it. */
    ImVec2 GetPauseButtonSize()
    {
        // The third argument leaves out the "###" part, which is not shown (imgui.h, CalcTextSize).
        const float labelWidth = std::max(ImGui::CalcTextSize(PauseLabel, nullptr, true).x,
                                          ImGui::CalcTextSize(ResumeLabel, nullptr, true).x);
        return {labelWidth + 2.0f * ImGui::GetStyle().FramePadding.x, 0.0f}; // a height of 0 is ImGui's own
    }

    /** The pause button: Pause while the world runs, Resume in the deep accent while paused; true on a click. */
    bool ShowPauseButton(bool isPaused)
    {
        if (!isPaused)
        {
            return ImGui::Button(PauseLabel, GetPauseButtonSize());
        }
        // The theme's held-button fill is its deep accent, which text stays readable on (tests/ui_tests.cpp).
        const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
        const bool isClicked = ImGui::Button(ResumeLabel, GetPauseButtonSize());
        ImGui::PopStyleColor(PausedColorCount);
        return isClicked;
    }
} // namespace

Wotah::ControlBarClicks Wotah::ShowControlBar(bool isPaused, bool canAddBall)
{
    // The middle of the window's bottom edge, a card's margin up.
    const ImVec2    display = ImGui::GetIO().DisplaySize;
    const glm::vec2 anchor{display.x / 2.0f, display.y - CardMargin};

    ControlBarClicks clicks;
    if (BeginCard(ControlBarName, anchor, BottomMiddlePivot))
    {
        ImGui::BeginDisabled(!canAddBall);
        clicks.AddBall = ImGui::Button(AddBallLabel);
        ImGui::EndDisabled();
        ImGui::SameLine();
        clicks.ResetScene = ImGui::Button(ResetSceneLabel);
        ImGui::SameLine();
        clicks.TogglePause = ShowPauseButton(isPaused);
        ImGui::SameLine();
        clicks.ResetCamera = ImGui::Button(ResetCameraLabel);
    }
    EndCard();
    return clicks;
}
