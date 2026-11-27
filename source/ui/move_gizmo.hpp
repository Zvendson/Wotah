/**
 * @file move_gizmo.hpp
 * The move gizmo on a point in the scene, drawn over it with ImGuizmo: a red, a green and a blue arrow along x, y and
 * z, a small square between each two of them and a ring in the middle. Dragging an arrow moves the point along it, a
 * square within its two axes, the ring across the view. Shown in a UI frame, between UserInterface::BeginFrame and
 * EndFrame. Over a handle and while one is dragged the UI keeps the mouse (ImGuizmo tells ImGui), so the camera and the
 * scene's clicks never get it.
 *
 *     glm::vec3 place = ...; // where the object is
 *     if (Wotah::ShowMoveGizmo(camera.GetView(), camera.GetProjection(), place))
 *     {
 *         // a handle is held: put the object at place
 *     }
 */
#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace Wotah
{
    /**
     * Puts the move gizmo at position into the UI frame, over the whole window, seen through a camera's view and
     * projection as render/camera.hpp gives them (Vulkan's clip space, y down). True while one of its handles is held
     * with the left mouse button; position then follows the mouse, and changes at no other time. One per UI frame.
     */
    [[nodiscard]] bool ShowMoveGizmo(const glm::mat4& view, const glm::mat4& projection, glm::vec3& position);
} // namespace Wotah
