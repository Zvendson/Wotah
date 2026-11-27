/**
 * @file mirror_view_tests.cpp
 * The mirrored view (render/mirror_view.hpp): what stands above the surface lands where the camera sees its mirror
 * image, between the near plane (in the surface) and the far plane; what lies below is cut off; no mirrored view from
 * below or on the surface.
 */
#include "render/camera.hpp"
#include "render/mirror_view.hpp"

#include <doctest/doctest.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <optional>

namespace
{
    /** A camera above the surface at height 2, looking down across it. */
    Wotah::Camera MakeCameraAbove()
    {
        Wotah::Camera camera;
        camera.SetPosition({1.0f, 5.0f, 8.0f});
        camera.SetTarget({0.0f, 0.0f, 0.0f});
        (void)camera.SetClipDistances(0.1f, 500.0f); // valid values
        return camera;
    }

    /** The surface's height in these tests. */
    constexpr float Surface = 2.0f;
} // namespace

TEST_SUITE("mirror view")
{
    TEST_CASE("what stands above the surface lands where the camera sees its mirror image, inside the depth range")
    {
        const Wotah::Camera            camera   = MakeCameraAbove();
        const std::optional<glm::mat4> mirrored = Wotah::MakeMirrorViewProjection(camera, Surface);
        REQUIRE(mirrored.has_value());
        for (const glm::vec3 point :
             {glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(-2.0f, 6.0f, -4.0f), glm::vec3(3.0f, 2.5f, -10.0f)})
        {
            CAPTURE(point.x);
            const glm::vec4 seen   = *mirrored * glm::vec4(point, 1.0f);
            const glm::vec3 image  = {point.x, 2.0f * Surface - point.y, point.z};
            const glm::vec4 direct = camera.GetViewProjection() * glm::vec4(image, 1.0f);
            CHECK(std::abs(seen.x / seen.w - direct.x / direct.w) < 1.0e-4f);
            CHECK(std::abs(seen.y / seen.w - direct.y / direct.w) < 1.0e-4f);
            CHECK(seen.z >= 0.0f);
            CHECK(seen.z <= seen.w);
        }
    }

    TEST_CASE("what lies below the surface is cut off by the mirrored view's near plane")
    {
        const std::optional<glm::mat4> mirrored = Wotah::MakeMirrorViewProjection(MakeCameraAbove(), Surface);
        REQUIRE(mirrored.has_value());
        for (const glm::vec3 point : {glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(2.0f, 0.0f, -5.0f)})
        {
            CAPTURE(point.y);
            CHECK((*mirrored * glm::vec4(point, 1.0f)).z < 0.0f);
        }
    }

    TEST_CASE("no mirrored view from below the surface or on it")
    {
        Wotah::Camera camera = MakeCameraAbove();
        camera.SetPosition({1.0f, Surface - 1.0f, 8.0f});
        CHECK_FALSE(Wotah::MakeMirrorViewProjection(camera, Surface).has_value());
        camera.SetPosition({1.0f, Surface + 0.005f, 8.0f});
        CHECK_FALSE(Wotah::MakeMirrorViewProjection(camera, Surface).has_value());
    }
}
