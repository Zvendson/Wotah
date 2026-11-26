/**
 * @file settings_window.cpp
 * The settings window: a card with a header, a thin line under it, and a two-column table of dim labels and their
 * sliders in three groups (light, physics, camera), then a button back to the defaults.
 */
#include "ui/settings_window.hpp"

#include "ui/card.hpp"

#include <glm/vec2.hpp>
#include <imgui.h>

namespace
{
    /** A slider's range and how its value is written (printf style), with its unit. */
    struct SliderRange
    {
        float       Min    = 0.0f;
        float       Max    = 1.0f;
        const char* Format = "%.2f";
    };

    // The ranges cover what is worth trying, chosen by eye. The unit signs are UTF-8 (degree "\xC2\xB0", squared
    // "\xC2\xB2", times "\xC3\x97"), from the Latin-1 Supplement, which the theme's font draws.
    constexpr SliderRange CompassRange{0.0f, 360.0f, "%.0f\xC2\xB0"};        // all the way round
    constexpr SliderRange ElevationRange{0.0f, 90.0f, "%.0f\xC2\xB0"};       // the horizon to straight overhead
    constexpr SliderRange BrightnessRange{0.0f, 2.0f, "%.2f"};               // dark to twice the scene's sun
    constexpr SliderRange SkyLightRange{0.0f, 3.0f, "%.2f\xC3\x97"};         // none to three times the scene's
    constexpr SliderRange GravityRange{0.0f, 25.0f, "%.2f m/s\xC2\xB2"};     // floating to Jupiter's 24.8
    constexpr SliderRange TimeSpeedRange{0.1f, 2.0f, "%.2f\xC3\x97"};        // slow motion to twice as fast
    constexpr SliderRange BouncinessRange{0.0f, 1.0f, "%.2f"};               // dead to bouncing back as high
    constexpr SliderRange TurnSpeedRange{-180.0f, 180.0f, "%.0f\xC2\xB0/s"}; // half a turn a second, either way
    constexpr SliderRange FieldOfViewRange{20.0f, 110.0f, "%.0f\xC2\xB0"};   // a telephoto lens to a wide angle

    /** The table's columns: the label and its slider. */
    constexpr int ColumnCount = 2;

    /** Every slider's width, so they line up and the window keeps its size whatever their values; chosen by eye. */
    constexpr float SliderWidth = 200.0f;

    /** Room above each group but the first, so the groups stand apart. */
    constexpr float GroupGap = 6.0f;

    /** Starts a row with its label, dim and level with the control that goes into the next column. */
    void BeginRow(const char* label)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(SliderWidth);
    }

    /** A group's name, bright, in a row of its own; room above it unless it is the first. */
    void ShowGroup(const char* name, bool isFirst)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (!isFirst)
        {
            ImGui::Dummy(ImVec2(0.0f, GroupGap));
        }
        ImGui::TextUnformatted(name);
    }

    /** A row with a slider for value, kept inside range; true when it moved. */
    bool ShowSlider(const char* label, float& value, const SliderRange& range)
    {
        BeginRow(label);
        ImGui::PushID(label); // the label names the slider for ImGui, as the slider shows none itself
        const bool isMoved =
            ImGui::SliderFloat("##value", &value, range.Min, range.Max, range.Format, ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopID();
        return isMoved;
    }

    /** A row with a check box for value; true when it was clicked. */
    bool ShowCheckBox(const char* label, bool& value)
    {
        BeginRow(label);
        ImGui::PushID(label);
        const bool isClicked = ImGui::Checkbox("##value", &value);
        ImGui::PopID();
        return isClicked;
    }

    // Each group draws all of its rows, and only then says whether any changed, so no row is ever skipped.

    /** The light: the sun's place and brightness, the sky's light, shadows. True when one changed. */
    bool ShowLightSettings(Wotah::SceneSettings& settings)
    {
        ShowGroup("Light", true);
        bool isChanged = ShowSlider("Sun direction", settings.SunAzimuthDegrees, CompassRange);
        isChanged      = ShowSlider("Sun height", settings.SunElevationDegrees, ElevationRange) || isChanged;
        isChanged      = ShowSlider("Sun brightness", settings.SunBrightness, BrightnessRange) || isChanged;
        isChanged      = ShowSlider("Sky light", settings.SkyLight, SkyLightRange) || isChanged;
        isChanged      = ShowCheckBox("Shadows", settings.HasShadows) || isChanged;
        return isChanged;
    }

    /** The physics: gravity, how fast time runs, the balls' bounce, the cube's turn. True when one changed. */
    bool ShowPhysicsSettings(Wotah::SceneSettings& settings)
    {
        ShowGroup("Physics", false);
        bool isChanged = ShowSlider("Gravity", settings.Gravity, GravityRange);
        isChanged      = ShowSlider("Time speed", settings.TimeSpeed, TimeSpeedRange) || isChanged;
        isChanged      = ShowSlider("Ball bounce", settings.BallBounciness, BouncinessRange) || isChanged;
        isChanged      = ShowSlider("Cube turn", settings.CubeTurnDegreesPerSecond, TurnSpeedRange) || isChanged;
        return isChanged;
    }

    /** The camera: its field of view. True when it changed. */
    bool ShowCameraSettings(Wotah::SceneSettings& settings)
    {
        ShowGroup("Camera", false);
        return ShowSlider("Field of view", settings.FieldOfViewDegrees, FieldOfViewRange);
    }
} // namespace

bool Wotah::ShowSettingsWindow(SceneSettings& settings, const SceneSettings& defaults)
{
    // The bottom-left corner of the window, a card's margin in.
    const glm::vec2 anchor{CardMargin, ImGui::GetIO().DisplaySize.y - CardMargin};

    // BeginTable says false while the table is out of sight; EndTable is due only after a true (imgui.h, BeginTable).
    bool isChanged = false;
    if (BeginCard(SettingsWindowTitle, anchor, BottomLeftPivot))
    {
        ImGui::TextUnformatted(SettingsWindowTitle);
        ImGui::Separator();
        if (ImGui::BeginTable("settings", ColumnCount))
        {
            isChanged = ShowLightSettings(settings);
            isChanged = ShowPhysicsSettings(settings) || isChanged;
            isChanged = ShowCameraSettings(settings) || isChanged;
            ImGui::EndTable();
        }
        if (ImGui::Button("Reset to defaults"))
        {
            settings  = defaults;
            isChanged = true;
        }
    }
    EndCard();
    return isChanged;
}
