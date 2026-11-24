/**
 * @file camera_tests.cpp
 * Camera: its matrices against glm directly, rebuilding after changes, independent copies, refused settings, and
 * keeping its last view when position, target and up give none.
 */
#include "render/camera.hpp"

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <limits>

namespace
{
    /** The view-projection glm gives directly, with Vulkan's flipped y, for comparison. */
    glm::mat4 MakeExpected(const glm::vec3& position, const glm::vec3& target, float nearest, float farthest)
    {
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, nearest, farthest);
        projection[1][1] *= -1.0f;
        return projection * glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    /** The example's corner camera. */
    Wotah::Camera MakeCornerCamera()
    {
        Wotah::Camera camera;
        camera.SetPosition({2.0f, 1.6f, 2.6f});
        camera.SetTarget({0.0f, 0.0f, 0.0f});
        (void)camera.SetClipDistances(0.1f, 10.0f); // valid values, checked below
        return camera;
    }

    /** Whether every number of a matrix is finite (no NaN, no infinity). */
    bool IsFinite(const glm::mat4& matrix)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!std::isfinite(matrix[column][row]))
                {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace

TEST_SUITE("camera")
{
    TEST_CASE("a camera gives the matrices glm gives, with Vulkan's flipped y")
    {
        const Wotah::Camera defaults;
        CHECK(defaults.GetViewProjection() == MakeExpected({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, 0.1f, 100.0f));
        CHECK(MakeCornerCamera().GetViewProjection() ==
              MakeExpected({2.0f, 1.6f, 2.6f}, {0.0f, 0.0f, 0.0f}, 0.1f, 10.0f));
    }

    TEST_CASE("changing a setting rebuilds the matrices")
    {
        Wotah::Camera   camera = MakeCornerCamera();
        const glm::mat4 before = camera.GetViewProjection();
        camera.SetPosition({-2.0f, 1.6f, 2.6f});
        CHECK(camera.GetViewProjection() != before);
        CHECK(camera.GetView() ==
              glm::lookAt(glm::vec3(-2.0f, 1.6f, 2.6f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    TEST_CASE("a copy changes alone")
    {
        const Wotah::Camera original = MakeCornerCamera();
        Wotah::Camera       copy     = original;
        copy.SetTarget({1.0f, 0.0f, 0.0f});
        CHECK(original.GetViewProjection() == MakeCornerCamera().GetViewProjection());
        CHECK(copy.GetViewProjection() != original.GetViewProjection());
    }

    TEST_CASE("bad settings are refused and change nothing")
    {
        Wotah::Camera camera = MakeCornerCamera();
        const float   nan    = std::numeric_limits<float>::quiet_NaN();

        CHECK_FALSE(camera.SetFieldOfView(0.0f));
        CHECK_FALSE(camera.SetFieldOfView(3.1416f)); // just above pi
        CHECK_FALSE(camera.SetFieldOfView(nan));
        CHECK_FALSE(camera.SetClipDistances(0.0f, 10.0f));
        CHECK_FALSE(camera.SetClipDistances(5.0f, 1.0f));
        CHECK_FALSE(camera.SetAspectRatio(0.0f));
        CHECK_FALSE(camera.SetAspectRatio(-1.0f));
        CHECK_FALSE(camera.SetAspectRatio(nan));
        CHECK(camera.GetViewProjection() == MakeCornerCamera().GetViewProjection());

        CHECK(camera.SetFieldOfView(1.0f));
        CHECK(camera.GetFieldOfView() == 1.0f);
    }

    TEST_CASE("a camera without a view keeps its last one, without NaNs")
    {
        Wotah::Camera   camera   = MakeCornerCamera();
        const glm::mat4 goodView = camera.GetView();

        SUBCASE("standing on its target")
        {
            camera.SetTarget({2.0f, 1.6f, 2.6f});
        }
        SUBCASE("up along the view direction")
        {
            camera.SetTarget({2.0f, 0.0f, 2.6f}); // straight down, with up = +y
        }
        CHECK(camera.GetView() == goodView);
        CHECK(IsFinite(camera.GetViewProjection()));
    }
}
