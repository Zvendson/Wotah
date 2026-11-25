/**
 * @file theme.hpp
 * The UI's look: a modern dark theme for Dear ImGui with slate panels in a thin light border, one water blue accent,
 * rounded corners and roomy spacing. UserInterface applies it when it starts ImGui.
 *
 *     ImGuiStyle style;
 *     Wotah::ApplyTheme(style); // or ApplyTheme(ImGui::GetStyle()) for the running UI
 *
 * ImGui stays inside theme.cpp; this header only names its style type.
 */
#pragma once

struct ImGuiStyle;

namespace Wotah
{
    /** The font size the theme sets, in pixels: a little larger than ImGui's 13, so text reads easily. */
    inline constexpr float ThemeFontSize = 15.0f;

    /**
     * Gives style the theme's colors, corners, spacing and font size. Every color starts from ImGui's dark theme first,
     * so colors a later ImGui version adds still fit.
     */
    void ApplyTheme(ImGuiStyle& style);
} // namespace Wotah
