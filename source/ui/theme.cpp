/**
 * @file theme.cpp
 * The theme's colors and sizes, all chosen by eye. tests/ui_tests.cpp checks that text stays readable on every fill it
 * is drawn over, and that the accent stands out from the fields it marks (WCAG 2.2 contrast ratios).
 */
#include "ui/theme.hpp"

#include <imgui.h>

namespace
{
    // ---- Colors (red, green, blue, opacity; 0..1) ----

    /** Window panels: a dark slate, slightly see-through, so the scene shows faintly behind them. */
    constexpr ImVec4 PanelColor{0.086f, 0.094f, 0.114f, 0.94f};

    /** Popups and tooltips: the panels' slate, nearly solid, as they lie over other windows. */
    constexpr ImVec4 PopupColor{0.086f, 0.094f, 0.114f, 0.98f};

    /** Title bars: a shade darker than the panels; the focused window's a shade lighter. */
    constexpr ImVec4 TitleColor{0.067f, 0.075f, 0.094f, 1.0f};
    constexpr ImVec4 FocusedTitleColor{0.102f, 0.114f, 0.137f, 1.0f};

    /** Fields, buttons and tabs: at rest, under the mouse, and held. */
    constexpr ImVec4 SurfaceColor{0.137f, 0.149f, 0.176f, 1.0f};
    constexpr ImVec4 HoveredSurfaceColor{0.173f, 0.188f, 0.220f, 1.0f};
    constexpr ImVec4 ActiveSurfaceColor{0.212f, 0.231f, 0.271f, 1.0f};

    /** Text, and text that is disabled or only labels something. */
    constexpr ImVec4 TextColor{0.902f, 0.910f, 0.925f, 1.0f};
    constexpr ImVec4 DimTextColor{0.541f, 0.569f, 0.620f, 1.0f};

    /** The thin edge around windows and popups: white, faint, but enough to set a panel off from a dark scene. */
    constexpr ImVec4 BorderColor{1.0f, 1.0f, 1.0f, 0.14f};

    /** Separators and lines inside panels: white, barely there. */
    constexpr ImVec4 LineColor{1.0f, 1.0f, 1.0f, 0.08f};

    /** Every other table row: white, fainter still. */
    constexpr ImVec4 AlternateRowColor{1.0f, 1.0f, 1.0f, 0.03f};

    /**
     * The accent, a water blue, for marks that show what is on or picked (check marks, grabs, plot lines); lighter
     * under the mouse. Too light to carry text: fills behind text take the deep accent.
     */
    constexpr ImVec4 AccentColor{0.227f, 0.608f, 0.863f, 1.0f};
    constexpr ImVec4 HoveredAccentColor{0.353f, 0.690f, 0.910f, 1.0f};
    constexpr ImVec4 DeepAccentColor{0.130f, 0.400f, 0.620f, 1.0f};

    /** Nothing: backgrounds that let their window show through. */
    constexpr ImVec4 NoColor{0.0f, 0.0f, 0.0f, 0.0f};

    /** How strongly the accent tints rows, grips and selected text: at rest, under the mouse, and picked or held. */
    constexpr float RestingTint  = 0.25f;
    constexpr float HoveredTint  = 0.40f;
    constexpr float SelectedTint = 0.55f;

    // ---- Sizes, in pixels ----

    /** Room inside windows, inside framed widgets (fields, buttons), between widgets, and inside table cells. */
    constexpr ImVec2 WindowPadding{14.0f, 12.0f};
    constexpr ImVec2 FramePadding{8.0f, 5.0f};
    constexpr ImVec2 ItemSpacing{8.0f, 6.0f};
    constexpr ImVec2 ItemInnerSpacing{6.0f, 4.0f};
    constexpr ImVec2 CellPadding{8.0f, 4.0f};

    /** Corners: windows rounder than the panels and widgets inside them. */
    constexpr float WindowRounding = 12.0f;
    constexpr float PanelRounding  = 8.0f; // child windows and popups
    constexpr float WidgetRounding = 6.0f; // fields, buttons, grabs, tabs

    /** Scrollbars: slim and fully round (their rounding is their width). */
    constexpr float ScrollbarWidth = 12.0f;

    /** The smallest slider or scrollbar grab, so it stays easy to catch. */
    constexpr float GrabMinSize = 10.0f;

    /** Borders: a thin line around windows and popups; widgets have none, their fill sets them apart. */
    constexpr float BorderWidth = 1.0f;
    constexpr float NoBorder    = 0.0f;

    /** The color at another opacity. */
    constexpr ImVec4 WithAlpha(const ImVec4& color, float alpha)
    {
        return {color.x, color.y, color.z, alpha};
    }

    /** The theme's colors; every slot not set here keeps ImGui's dark theme. */
    void ApplyColors(ImGuiStyle& style)
    {
        ImVec4* colors = style.Colors;

        colors[ImGuiCol_Text]            = TextColor;
        colors[ImGuiCol_TextDisabled]    = DimTextColor;
        colors[ImGuiCol_TextLink]        = HoveredAccentColor;
        colors[ImGuiCol_TextSelectedBg]  = WithAlpha(AccentColor, SelectedTint);
        colors[ImGuiCol_InputTextCursor] = TextColor;

        colors[ImGuiCol_WindowBg]         = PanelColor;
        colors[ImGuiCol_ChildBg]          = NoColor;
        colors[ImGuiCol_PopupBg]          = PopupColor;
        colors[ImGuiCol_Border]           = BorderColor;
        colors[ImGuiCol_BorderShadow]     = NoColor;
        colors[ImGuiCol_TitleBg]          = TitleColor;
        colors[ImGuiCol_TitleBgActive]    = FocusedTitleColor;
        colors[ImGuiCol_TitleBgCollapsed] = TitleColor;
        colors[ImGuiCol_MenuBarBg]        = TitleColor;

        colors[ImGuiCol_FrameBg]        = SurfaceColor;
        colors[ImGuiCol_FrameBgHovered] = HoveredSurfaceColor;
        colors[ImGuiCol_FrameBgActive]  = ActiveSurfaceColor;
        colors[ImGuiCol_Button]         = SurfaceColor;
        colors[ImGuiCol_ButtonHovered]  = HoveredSurfaceColor;
        colors[ImGuiCol_ButtonActive]   = DeepAccentColor;

        colors[ImGuiCol_CheckMark]        = AccentColor;
        colors[ImGuiCol_SliderGrab]       = AccentColor;
        colors[ImGuiCol_SliderGrabActive] = HoveredAccentColor;

        colors[ImGuiCol_Header]        = WithAlpha(AccentColor, RestingTint);
        colors[ImGuiCol_HeaderHovered] = WithAlpha(AccentColor, HoveredTint);
        colors[ImGuiCol_HeaderActive]  = WithAlpha(AccentColor, SelectedTint);

        colors[ImGuiCol_Separator]         = LineColor;
        colors[ImGuiCol_SeparatorHovered]  = WithAlpha(AccentColor, HoveredTint);
        colors[ImGuiCol_SeparatorActive]   = AccentColor;
        colors[ImGuiCol_ResizeGrip]        = WithAlpha(AccentColor, RestingTint);
        colors[ImGuiCol_ResizeGripHovered] = WithAlpha(AccentColor, HoveredTint);
        colors[ImGuiCol_ResizeGripActive]  = WithAlpha(AccentColor, SelectedTint);

        colors[ImGuiCol_ScrollbarBg]          = NoColor;
        colors[ImGuiCol_ScrollbarGrab]        = HoveredSurfaceColor;
        colors[ImGuiCol_ScrollbarGrabHovered] = ActiveSurfaceColor;
        colors[ImGuiCol_ScrollbarGrabActive]  = AccentColor;

        colors[ImGuiCol_Tab]                       = SurfaceColor;
        colors[ImGuiCol_TabHovered]                = HoveredSurfaceColor;
        colors[ImGuiCol_TabSelected]               = ActiveSurfaceColor;
        colors[ImGuiCol_TabSelectedOverline]       = AccentColor;
        colors[ImGuiCol_TabDimmed]                 = TitleColor;
        colors[ImGuiCol_TabDimmedSelected]         = SurfaceColor;
        colors[ImGuiCol_TabDimmedSelectedOverline] = LineColor;

        colors[ImGuiCol_PlotLines]            = AccentColor;
        colors[ImGuiCol_PlotLinesHovered]     = HoveredAccentColor;
        colors[ImGuiCol_PlotHistogram]        = AccentColor;
        colors[ImGuiCol_PlotHistogramHovered] = HoveredAccentColor;

        colors[ImGuiCol_TableHeaderBg]     = SurfaceColor;
        colors[ImGuiCol_TableBorderStrong] = ActiveSurfaceColor;
        colors[ImGuiCol_TableBorderLight]  = HoveredSurfaceColor;
        colors[ImGuiCol_TableRowBg]        = NoColor;
        colors[ImGuiCol_TableRowBgAlt]     = AlternateRowColor;

        colors[ImGuiCol_DragDropTarget] = HoveredAccentColor;
        colors[ImGuiCol_NavCursor]      = AccentColor;
    }

    /** The theme's spacing, corners and borders. */
    void ApplySizes(ImGuiStyle& style)
    {
        style.WindowPadding    = WindowPadding;
        style.FramePadding     = FramePadding;
        style.ItemSpacing      = ItemSpacing;
        style.ItemInnerSpacing = ItemInnerSpacing;
        style.CellPadding      = CellPadding;

        style.WindowRounding    = WindowRounding;
        style.ChildRounding     = PanelRounding;
        style.PopupRounding     = PanelRounding;
        style.FrameRounding     = WidgetRounding;
        style.GrabRounding      = WidgetRounding;
        style.TabRounding       = WidgetRounding;
        style.ScrollbarSize     = ScrollbarWidth;
        style.ScrollbarRounding = ScrollbarWidth;
        style.GrabMinSize       = GrabMinSize;

        style.WindowBorderSize = BorderWidth;
        style.ChildBorderSize  = BorderWidth;
        style.PopupBorderSize  = BorderWidth;
        style.FrameBorderSize  = NoBorder;
        style.TabBorderSize    = NoBorder;
    }
} // namespace

void Wotah::ApplyTheme(ImGuiStyle& style)
{
    // ImGui's dark theme fills every color slot first, also those this theme does not know yet.
    ImGui::StyleColorsDark(&style);
    ApplyColors(style);
    ApplySizes(style);
    style.FontSizeBase = ThemeFontSize;
}
