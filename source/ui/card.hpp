/**
 * @file card.hpp
 * The UI's cards: small windows fixed in place, without a title bar, as large as what they hold, in the theme's thin
 * border and with a soft shadow under them. The stats window, the hints window and the control bar are cards.
 *
 *     if (Wotah::BeginCard("Stats", position, Wotah::TopLeftPivot))
 *     {
 *         ImGui::TextUnformatted("Hello");
 *     }
 *     Wotah::EndCard(); // either way
 *
 * ImGui stays inside card.cpp.
 */
#pragma once

#include <glm/vec2.hpp>

namespace Wotah
{
    /** How far cards keep from the window's edges, in the cursor's units. */
    inline constexpr float CardMargin = 12.0f;

    /** Which point of a card sits on its anchor (0..1 across and down the card): its top-left corner. */
    inline constexpr glm::vec2 TopLeftPivot{0.0f, 0.0f};

    /** Which point of a card sits on its anchor: its top-right corner. */
    inline constexpr glm::vec2 TopRightPivot{1.0f, 0.0f};

    /** Which point of a card sits on its anchor: the middle of its bottom edge. */
    inline constexpr glm::vec2 BottomMiddlePivot{0.5f, 1.0f};

    /**
     * Starts a card called name (unique among the UI's windows; it is not shown) with its pivot point on anchor, in the
     * cursor's units. The card cannot be folded, moved or resized, and it takes no focus when it appears. True when its
     * contents should be added; EndCard is due either way.
     */
    [[nodiscard]] bool BeginCard(const char* name, const glm::vec2& anchor, const glm::vec2& pivot);

    /** Ends the card BeginCard started, whatever it answered. */
    void EndCard();
} // namespace Wotah
