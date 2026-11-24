/**
 * @file camera.hpp
 * A perspective camera: where it stands, what it looks at, how wide it sees. It gives the view-projection matrix in
 * Vulkan's conventions (y down in clip space, depth 0..1). A plain value: copy it, keep several, change any of them.
 *
 *     Wotah::Camera camera;
 *     camera.SetPosition({2.0f, 1.6f, 2.6f});
 *     camera.SetTarget({0.0f, 0.0f, 0.0f});
 *     camera.SetAspectRatio(16.0f / 9.0f);
 *     const glm::mat4& viewProjection = camera.GetViewProjection();
 *
 * The matrices are only rebuilt when a setting changed since they were last asked for.
 */
#pragma once

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

namespace Wotah
{
    /**
     * A perspective camera. Every setting has a usable default: at (0, 0, 3), looking at the origin, y up, a 45 degree
     * field of view, seeing from 0.1 to 100 units, square aspect ratio.
     */
    class Camera final
    {
    public:
        /** Where the camera stands, in world units. */
        void SetPosition(const glm::vec3& position) noexcept;

        /** The point the camera looks at. */
        void SetTarget(const glm::vec3& target) noexcept;

        /** Which way is up for the camera; it must not point along the view direction. */
        void SetUp(const glm::vec3& up) noexcept;

        /** The vertical field of view in radians, between 0 and pi; false (and logged) otherwise. */
        bool SetFieldOfView(float radians);

        /** The nearest and farthest distance the camera sees; false (and logged) unless 0 < nearest < farthest. */
        bool SetClipDistances(float nearest, float farthest);

        /** The image's width divided by its height; false (and logged) unless above 0. */
        bool SetAspectRatio(float widthByHeight);

        /** Where the camera stands. */
        [[nodiscard]] const glm::vec3& GetPosition() const noexcept { return m_position; }

        /** The point the camera looks at. */
        [[nodiscard]] const glm::vec3& GetTarget() const noexcept { return m_target; }

        /** Which way is up for the camera. */
        [[nodiscard]] const glm::vec3& GetUp() const noexcept { return m_up; }

        /** The vertical field of view in radians. */
        [[nodiscard]] float GetFieldOfView() const noexcept { return m_fieldOfView; }

        /** The nearest distance the camera sees. */
        [[nodiscard]] float GetNearDistance() const noexcept { return m_nearDistance; }

        /** The farthest distance the camera sees. */
        [[nodiscard]] float GetFarDistance() const noexcept { return m_farDistance; }

        /** The image's width divided by its height. */
        [[nodiscard]] float GetAspectRatio() const noexcept { return m_aspectRatio; }

        /** World space to camera space. */
        [[nodiscard]] const glm::mat4& GetView() const;

        /** Camera space to Vulkan clip space. */
        [[nodiscard]] const glm::mat4& GetProjection() const;

        /** World space to Vulkan clip space: GetProjection() * GetView(). */
        [[nodiscard]] const glm::mat4& GetViewProjection() const;

    private:
        /** Rebuilds the matrices when a setting changed since the last time. */
        void UpdateMatrices() const;

        glm::vec3 m_position{0.0f, 0.0f, 3.0f};
        glm::vec3 m_target{0.0f, 0.0f, 0.0f};
        glm::vec3 m_up{0.0f, 1.0f, 0.0f};
        float     m_fieldOfView  = glm::radians(45.0f);
        float     m_nearDistance = 0.1f;
        float     m_farDistance  = 100.0f;
        float     m_aspectRatio  = 1.0f;

        // A cache of what the settings above give; mutable so the getters stay const.
        mutable glm::mat4 m_view{1.0f};
        mutable glm::mat4 m_projection{1.0f};
        mutable glm::mat4 m_viewProjection{1.0f};
        mutable bool      m_isOutdated = true;
    };
} // namespace Wotah
