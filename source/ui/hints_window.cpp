/**
 * @file hints_window.cpp
 * The hints window: a card with a header, a thin line under it, and a two-column table of key caps and what they do.
 * The controls listed are those of input/camera_controller.hpp and input/scene_shortcuts.hpp (R, Tab for the settings
 * window, B, and the left mouse button, which picks a ball for ui/move_gizmo.hpp's arrows).
 */
#include "ui/hints_window.hpp"

#include "ui/card.hpp"

#include <glm/vec2.hpp>
#include <imgui.h>

#include <array>
#include <cstddef>
#include <span>

namespace
{
    /** One control: the keys or buttons, each shown as a key cap, and what they do. */
    struct Hint
    {
        std::span<const char* const> Keys;
        const char*                  Action = "";
    };

    /** The keys of each control. */
    constexpr std::array<const char*, 4> MoveKeys{"W", "A", "S", "D"};
    constexpr std::array<const char*, 1> UpKeys{"Space"};
    constexpr std::array<const char*, 1> DownKeys{"Shift"};
    constexpr std::array<const char*, 1> TurnKeys{"Right mouse"};
    constexpr std::array<const char*, 1> ZoomKeys{"Wheel"};
    constexpr std::array<const char*, 1> CameraResetKeys{"F"};
    constexpr std::array<const char*, 1> SceneResetKeys{"R"};
    constexpr std::array<const char*, 1> SettingsKeys{"Tab"};
    constexpr std::array<const char*, 1> AddBallKeys{"B"};
    constexpr std::array<const char*, 1> PickKeys{"Left mouse"};

    /** Every control, in the order they are listed. */
    constexpr std::array<Hint, 10> Hints{{
        {MoveKeys, "Move"},
        {UpKeys, "Up"},
        {DownKeys, "Down"},
        {TurnKeys, "Look around (hold)"},
        {ZoomKeys, "Closer / farther"},
        {CameraResetKeys, "Reset camera"},
        {SceneResetKeys, "Reset scene"},
        {SettingsKeys, "Settings"},
        {AddBallKeys, "Add a ball"},
        {PickKeys, "Pick a ball, drag its arrows"},
    }};

    /** The table's columns: the keys and what they do. */
    constexpr int ColumnCount = 2;

    /** Room between a key cap's edge and its name, chosen by eye: snug, as on a keyboard. */
    constexpr ImVec2 KeyCapPadding{5.0f, 1.0f};

    /** Space between the key caps of one control, as between W, A, S and D. */
    constexpr float KeyCapGap = 4.0f;

    /** Draws a key as a small cap: its name on the fields' fill, in the theme's thin border, rounded like a widget. */
    void ShowKeyCap(const char* key)
    {
        const ImVec2 nameSize = ImGui::CalcTextSize(key);
        const ImVec2 size(nameSize.x + 2.0f * KeyCapPadding.x, nameSize.y + 2.0f * KeyCapPadding.y);
        const ImVec2 topLeft = ImGui::GetCursorScreenPos();
        const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
        const float  rounding = ImGui::GetStyle().FrameRounding;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(topLeft, bottomRight, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
        drawList->AddRect(topLeft, bottomRight, ImGui::GetColorU32(ImGuiCol_Border), rounding);
        drawList->AddText(ImVec2(topLeft.x + KeyCapPadding.x, topLeft.y + KeyCapPadding.y),
                          ImGui::GetColorU32(ImGuiCol_Text), key);
        ImGui::Dummy(size); // takes the cap's room in the layout
    }

    /** One table row: the control's key caps side by side, then what they do, level with the caps' names. */
    void ShowHint(const Hint& hint)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        for (std::size_t index = 0; index < hint.Keys.size(); ++index)
        {
            if (index > 0)
            {
                ImGui::SameLine(0.0f, KeyCapGap);
            }
            ShowKeyCap(hint.Keys[index]);
        }
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + KeyCapPadding.y);
        ImGui::TextUnformatted(hint.Action);
    }
} // namespace

void Wotah::ShowHintsWindow()
{
    // The top-right corner of the window, a card's margin in.
    const ImVec2    display = ImGui::GetIO().DisplaySize;
    const glm::vec2 anchor{display.x - CardMargin, CardMargin};

    // BeginTable says false while the table is out of sight; EndTable is due only after a true (imgui.h, BeginTable).
    if (BeginCard(HintsWindowTitle, anchor, TopRightPivot))
    {
        ImGui::TextUnformatted(HintsWindowTitle);
        ImGui::Separator();
        if (ImGui::BeginTable("hints", ColumnCount))
        {
            for (const Hint& hint : Hints)
            {
                ShowHint(hint);
            }
            ImGui::EndTable();
        }
    }
    EndCard();
}
