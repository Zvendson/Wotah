/**
 * @file move_gizmo.cpp
 * The move gizmo, on ImGuizmo's Manipulate.
 */
#include "ui/move_gizmo.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/vec4.hpp>
#include <imgui.h>

// ImGuizmo's header uses ImGui's types without including imgui.h itself.
#include <ImGuizmo.h>

namespace
{
    /** The rows of a 4 x 4 matrix; the second is clip space's y. */
    constexpr int MatrixSize = 4;
    constexpr int ClipYRow   = 1;

    /**
     * How long the arrows are on the screen, the same near and far: in ImGuizmo's clip-space size, where the picture's
     * wider side is 2 (GetSegmentLengthClipSpace), so 0.15 is 7.5 % of it. By eye: half as long again as ImGuizmo's own
     * 0.1, so they reach out of a ball seen from a few metres and are easy to grab.
     */
    constexpr float ArrowSize = 0.15f;
} // namespace

bool Wotah::ShowMoveGizmo(const glm::mat4& view, const glm::mat4& projection, glm::vec3& position)
{
    // ImGuizmo works in OpenGL's clip space, y up (ImGuizmo.cpp, worldToPos): the camera's y row is turned back over.
    glm::mat4 upright = projection;
    for (int column = 0; column < MatrixSize; ++column)
    {
        upright[column][ClipYRow] = -upright[column][ClipYRow];
    }

    // Over the whole window, in the cursor's units, as ImGui lays its windows out (UserInterface::BeginFrame).
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetGizmoSizeClipSpace(ArrowSize);
    ImGuizmo::SetRect(0.0f, 0.0f, display.x, display.y);

    // The point's place only: the arrows stay along the world's axes (WORLD), however the object is turned.
    glm::mat4 model(1.0f);
    model[3] = glm::vec4(position, 1.0f);
    (void)ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(upright), ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                               glm::value_ptr(model));

    // In the frame the button is let go, ImGuizmo still moves the point once, to where the mouse meets its drag plane;
    // after a drag the gizmo was hidden for, that plane may be long out of date. So only a held drag moves it.
    const bool isHeld = ImGuizmo::IsUsing();
    if (isHeld && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        position = glm::vec3(model[3]);
    }
    return isHeld;
}
