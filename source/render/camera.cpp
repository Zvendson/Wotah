/**
 * @file camera.cpp
 * The camera's settings checks and its matrices, built with glm.
 */
#include "render/camera.hpp"

#include "core/logger.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace
{
    /** Below this squared length a direction counts as zero, so no view can be built from it. */
    constexpr float NearlyZeroSquared = 1.0e-12f;
} // namespace

void Wotah::Camera::SetPosition(const glm::vec3& position) noexcept
{
    m_position   = position;
    m_isOutdated = true;
}

void Wotah::Camera::SetTarget(const glm::vec3& target) noexcept
{
    m_target     = target;
    m_isOutdated = true;
}

void Wotah::Camera::SetUp(const glm::vec3& up) noexcept
{
    m_up         = up;
    m_isOutdated = true;
}

bool Wotah::Camera::SetFieldOfView(float radians)
{
    if (!(radians > 0.0f && radians < glm::pi<float>())) // written this way so NaN is refused too
    {
        Logger::Error("A camera's field of view must be between 0 and pi radians, not {}", radians);
        return false;
    }
    m_fieldOfView = radians;
    m_isOutdated  = true;
    return true;
}

bool Wotah::Camera::SetClipDistances(float nearest, float farthest)
{
    if (!(nearest > 0.0f && farthest > nearest))
    {
        Logger::Error("A camera needs 0 < nearest < farthest, not {} and {}", nearest, farthest);
        return false;
    }
    m_nearDistance = nearest;
    m_farDistance  = farthest;
    m_isOutdated   = true;
    return true;
}

bool Wotah::Camera::SetAspectRatio(float widthByHeight)
{
    if (!(widthByHeight > 0.0f))
    {
        Logger::Error("A camera's aspect ratio must be above 0, not {}", widthByHeight);
        return false;
    }
    if (widthByHeight != m_aspectRatio) // set every frame, so an unchanged value keeps the matrices
    {
        m_aspectRatio = widthByHeight;
        m_isOutdated  = true;
    }
    return true;
}

const glm::mat4& Wotah::Camera::GetView() const
{
    UpdateMatrices();
    return m_view;
}

const glm::mat4& Wotah::Camera::GetProjection() const
{
    UpdateMatrices();
    return m_projection;
}

const glm::mat4& Wotah::Camera::GetViewProjection() const
{
    UpdateMatrices();
    return m_viewProjection;
}

glm::vec3 Wotah::Camera::GetPictureDirection(const glm::vec2& picturePlace) const
{
    // The view's rows are the camera's right, up and backward directions (glm::lookAt). One unit ahead, the picture
    // reaches tan(half the field of view) up and down, and the aspect ratio times that to each side (glm::perspective).
    const glm::mat4& view     = GetView();
    const glm::vec3  right    = {view[0][0], view[1][0], view[2][0]};
    const glm::vec3  up       = {view[0][1], view[1][1], view[2][1]};
    const glm::vec3  backward = {view[0][2], view[1][2], view[2][2]};
    const float      reach    = std::tan(m_fieldOfView / 2.0f);
    const glm::vec2  centered = {2.0f * picturePlace.x - 1.0f, 1.0f - 2.0f * picturePlace.y}; // y up, -1 to 1
    return glm::normalize(-backward + centered.x * reach * m_aspectRatio * right + centered.y * reach * up);
}

void Wotah::Camera::UpdateMatrices() const
{
    if (!m_isOutdated)
    {
        return;
    }
    m_isOutdated = false;

    // A camera on its target, or with up along the view direction, has no defined view: keep the last good one, so
    // the picture stays put instead of turning into NaNs. Logged once per change, not every frame.
    const glm::vec3 viewDirection = m_target - m_position;
    const glm::vec3 side          = glm::cross(viewDirection, m_up);
    if (glm::dot(viewDirection, viewDirection) < NearlyZeroSquared || glm::dot(side, side) < NearlyZeroSquared)
    {
        Logger::Warning("The camera's position, target and up give no view; it keeps its last view");
    }
    else
    {
        m_view = glm::lookAt(m_position, m_target, m_up);
    }

    // glm follows OpenGL, whose clip space points y up; Vulkan's points y down. Flipping y keeps the picture upright,
    // and keeps counter-clockwise triangles counter-clockwise on screen. The 0..1 depth range comes from
    // GLM_FORCE_DEPTH_ZERO_TO_ONE (CMakeLists.txt).
    m_projection = glm::perspective(m_fieldOfView, m_aspectRatio, m_nearDistance, m_farDistance);
    m_projection[1][1] *= -1.0f;

    m_viewProjection = m_projection * m_view;
}
