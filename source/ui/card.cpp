/**
 * @file card.cpp
 * Cards: ImGui windows with the card's flags, placed every frame, with a shadow drawn behind them. ImGui 1.93 has no
 * window shadows of its own, so the shadow is drawn here.
 */
#include "ui/card.hpp"

#include <imgui.h>

namespace
{
    /**
     * A fixed card: no title bar (so nothing to fold it by), no moving or resizing, as large as what it holds, and no
     * focus taken when it first appears.
     */
    constexpr ImGuiWindowFlags CardFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

    /**
     * The shadow, chosen by eye: layers of see-through black, each a little larger than the card and the one before,
     * and dropped a few pixels, so it is darkest under the card and fades out softly around it.
     */
    constexpr int   ShadowLayers       = 8;
    constexpr float ShadowLayerSpread  = 1.5f;   // pixels each layer reaches further than the one before
    constexpr float ShadowDrop         = 4.0f;   // pixels the shadow lies below the card
    constexpr float ShadowLayerOpacity = 0.035f; // the layers add up to about 25 % under the card

    /** Draws the soft shadow of a window at position and size behind every window. */
    void DrawShadow(const ImVec2& position, const ImVec2& size)
    {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        const float rounding = ImGui::GetStyle().WindowRounding;
        const ImU32 color    = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, ShadowLayerOpacity));
        for (int layer = 1; layer <= ShadowLayers; ++layer)
        {
            const float  spread = ShadowLayerSpread * static_cast<float>(layer);
            const ImVec2 topLeft(position.x - spread, position.y - spread + ShadowDrop);
            const ImVec2 bottomRight(position.x + size.x + spread, position.y + size.y + spread + ShadowDrop);
            drawList->AddRectFilled(topLeft, bottomRight, color, rounding + spread);
        }
    }
} // namespace

bool Wotah::BeginCard(const char* name, const glm::vec2& anchor, const glm::vec2& pivot)
{
    // Placed every frame, so the card follows the window's edges when the window is resized.
    ImGui::SetNextWindowPos(ImVec2(anchor.x, anchor.y), ImGuiCond_Always, ImVec2(pivot.x, pivot.y));

    // Begin says false while the card is out of sight; End is due either way (imgui.h, Begin).
    const bool isOpen = ImGui::Begin(name, nullptr, CardFlags);

    // ImGui measures a window that sizes itself in the frame it appears and hides it meanwhile (imgui.cpp, Begin), so
    // the shadow waits for the card's real size.
    if (isOpen && !ImGui::IsWindowAppearing())
    {
        DrawShadow(ImGui::GetWindowPos(), ImGui::GetWindowSize());
    }
    return isOpen;
}

void Wotah::EndCard()
{
    ImGui::End();
}
