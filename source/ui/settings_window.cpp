/**
 * @file settings_window.cpp
 * The settings window: a card with a header, a thin line under it, tabs (sky, physics, camera) each with a two-column
 * table of dim labels and their sliders, then a button back to the defaults.
 */
#include "ui/settings_window.hpp"

#include "ui/card.hpp"

#include <glm/vec2.hpp>
#include <imgui.h>

#include <cmath>

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
    constexpr SliderRange DayLengthRange{1.0f, 60.0f, "%.0f min"};           // a quick look to a slow hour
    constexpr SliderRange NoonHeightRange{10.0f, 90.0f, "%.0f\xC2\xB0"};     // a winter's day far north to the tropics
    constexpr SliderRange BrightnessRange{0.0f, 2.0f, "%.2f"};               // dark to twice the scene's sun
    constexpr SliderRange ShareRange{0.0f, 3.0f, "%.2f\xC3\x97"};            // none to three times the scene's
    constexpr SliderRange GravityRange{0.0f, 25.0f, "%.2f m/s\xC2\xB2"};     // floating to Jupiter's 24.8
    constexpr SliderRange TimeSpeedRange{0.1f, 2.0f, "%.2f\xC3\x97"};        // slow motion to twice as fast
    constexpr SliderRange BouncinessRange{0.0f, 1.0f, "%.2f"};               // dead to bouncing back as high
    constexpr SliderRange TurnSpeedRange{-180.0f, 180.0f, "%.0f\xC2\xB0/s"}; // half a turn a second, either way
    constexpr SliderRange FieldOfViewRange{20.0f, 110.0f, "%.0f\xC2\xB0"};   // a telephoto lens to a wide angle

    /** The time of day's slider: midnight to midnight; its text comes from MakeClockText. */
    constexpr float FirstHour = 0.0f;
    constexpr float LastHour  = 24.0f;

    /** Minutes in an hour and in a day, for the clock's text. */
    constexpr int MinutesPerHour = 60;
    constexpr int MinutesPerDay  = 24 * MinutesPerHour;

    /** The base the clock's digits count in. */
    constexpr int DecimalBase = 10;

    /** The character of one decimal digit, 0 to 9. */
    char MakeDigit(int digit)
    {
        return static_cast<char>('0' + digit);
    }

    /** The table's columns: the label and its slider. */
    constexpr int ColumnCount = 2;

    /** Every slider's width, so they line up and the window keeps its width whatever their values; chosen by eye. */
    constexpr float SliderWidth = 200.0f;

    /** Starts a table's row with its label, dim and level with the control that goes into the next column. */
    void BeginRow(const char* label)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(SliderWidth);
    }

    /** A row with a slider for value, kept inside min and max, its value written by format; true when it moved. */
    bool ShowSlider(const char* label, float& value, float min, float max, const char* format)
    {
        BeginRow(label);
        ImGui::PushID(label); // the label names the slider for ImGui, as the slider shows none itself
        const bool isMoved = ImGui::SliderFloat("##value", &value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopID();
        return isMoved;
    }

    /** A row with a slider for value, kept inside range; true when it moved. */
    bool ShowSlider(const char* label, float& value, const SliderRange& range)
    {
        return ShowSlider(label, value, range.Min, range.Max, range.Format);
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

    // Each tab draws all of its rows, and only then says whether any changed, so no row is ever skipped.

    /**
     * The sky: the time of day (as a clock), whether the day runs and how long it lasts, how high the sun gets at noon,
     * how bright sun, moon and sky are, shadows. True when one changed.
     */
    bool ShowSkyRows(Wotah::SceneSettings& settings)
    {
        // The slider's text is the clock, not a printf pattern: it holds no "%", so ImGui shows it as it is.
        const Wotah::ClockText clock = Wotah::MakeClockText(settings.TimeOfDayHours);
        bool isChanged = ShowSlider("Time of day", settings.TimeOfDayHours, FirstHour, LastHour, clock.data());
        isChanged      = ShowCheckBox("Day runs", settings.IsDayRunning) || isChanged;
        isChanged      = ShowSlider("Day length", settings.DayLengthMinutes, DayLengthRange) || isChanged;
        isChanged      = ShowSlider("Sun at noon", settings.SunNoonHeightDegrees, NoonHeightRange) || isChanged;
        isChanged      = ShowSlider("Sun brightness", settings.SunBrightness, BrightnessRange) || isChanged;
        isChanged      = ShowSlider("Moon brightness", settings.MoonBrightness, BrightnessRange) || isChanged;
        isChanged      = ShowSlider("Sky light", settings.SkyLight, ShareRange) || isChanged;
        isChanged      = ShowCheckBox("Shadows", settings.HasShadows) || isChanged;
        return isChanged;
    }

    /** The physics: gravity, how fast time runs, the balls' bounce, the cube's turn. True when one changed. */
    bool ShowPhysicsRows(Wotah::SceneSettings& settings)
    {
        bool isChanged = ShowSlider("Gravity", settings.Gravity, GravityRange);
        isChanged      = ShowSlider("Time speed", settings.TimeSpeed, TimeSpeedRange) || isChanged;
        isChanged      = ShowSlider("Ball bounce", settings.BallBounciness, BouncinessRange) || isChanged;
        isChanged      = ShowSlider("Cube turn", settings.CubeTurnDegreesPerSecond, TurnSpeedRange) || isChanged;
        return isChanged;
    }

    /** The camera: its field of view. True when it changed. */
    bool ShowCameraRows(Wotah::SceneSettings& settings)
    {
        return ShowSlider("Field of view", settings.FieldOfViewDegrees, FieldOfViewRange);
    }

    /** A tab's rows: they show settings in a table and say whether one changed. */
    using RowsFunction = bool (*)(Wotah::SceneSettings& settings);

    /**
     * Shows a tab with its rows while it is the open one; true when a setting in it changed. BeginTable says false
     * while the table is out of sight; EndTable is due only after a true, and EndTabItem only after BeginTabItem said
     * true (imgui.h).
     */
    bool ShowTab(const char* name, RowsFunction showRows, Wotah::SceneSettings& settings)
    {
        if (!ImGui::BeginTabItem(name))
        {
            return false;
        }
        bool isChanged = false;
        if (ImGui::BeginTable(name, ColumnCount))
        {
            isChanged = showRows(settings);
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
        return isChanged;
    }
} // namespace

Wotah::ClockText Wotah::MakeClockText(float hours)
{
    // Whole minutes into the day, wrapped past midnight either way.
    const float minutes   = std::isfinite(hours) ? std::floor(hours * static_cast<float>(MinutesPerHour)) : 0.0f;
    const float wrapped   = std::fmod(minutes, static_cast<float>(MinutesPerDay));
    const int   dayMinute = static_cast<int>(wrapped < 0.0f ? wrapped + static_cast<float>(MinutesPerDay) : wrapped);

    const int hour   = dayMinute / MinutesPerHour;
    const int minute = dayMinute % MinutesPerHour;
    return {MakeDigit(hour / DecimalBase),   MakeDigit(hour % DecimalBase),   ':',
            MakeDigit(minute / DecimalBase), MakeDigit(minute % DecimalBase), '\0'};
}

bool Wotah::ShowSettingsWindow(SceneSettings& settings, const SceneSettings& defaults)
{
    // The bottom-left corner of the window, a card's margin in; the card grows upwards when a tab has more rows.
    const glm::vec2 anchor{CardMargin, ImGui::GetIO().DisplaySize.y - CardMargin};

    bool isChanged = false;
    if (BeginCard(SettingsWindowTitle, anchor, BottomLeftPivot))
    {
        ImGui::TextUnformatted(SettingsWindowTitle);
        ImGui::Separator();
        if (ImGui::BeginTabBar("groups"))
        {
            isChanged = ShowTab("Sky", ShowSkyRows, settings);
            isChanged = ShowTab("Physics", ShowPhysicsRows, settings) || isChanged;
            isChanged = ShowTab("Camera", ShowCameraRows, settings) || isChanged;
            ImGui::EndTabBar();
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
