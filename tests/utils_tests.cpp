/**
 * @file utils_tests.cpp
 * The helpers in core/utils.hpp.
 */
#include "core/utils.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>

TEST_SUITE("utils")
{
    TEST_CASE("the executable's folder holds this test program")
    {
        const std::optional<std::filesystem::path> folder = Wotah::FindExecutableDirectory();
        REQUIRE(folder.has_value());
        CHECK(std::filesystem::is_directory(*folder));
        CHECK(
            (std::filesystem::exists(*folder / "wotah_tests.exe") || std::filesystem::exists(*folder / "wotah_tests")));
    }

    TEST_CASE("turning an angle: on by speed times time, always within one turn")
    {
        constexpr float FullTurn = 2.0f * std::numbers::pi_v<float>;
        constexpr float Margin   = 1.0e-5f;

        // Plain steps forward and back.
        CHECK(std::abs(Wotah::TurnAngle(0.0f, 1.0f, 0.5f) - 0.5f) < Margin);
        CHECK(std::abs(Wotah::TurnAngle(1.0f, -2.0f, 0.25f) - 0.5f) < Margin);
        CHECK(Wotah::TurnAngle(1.0f, 3.0f, 0.0f) == 1.0f);

        // Past a full turn it starts again from 0; below 0 it comes back from the top.
        CHECK(std::abs(Wotah::TurnAngle(6.0f, 1.0f, 1.0f) - (7.0f - FullTurn)) < Margin);
        CHECK(std::abs(Wotah::TurnAngle(0.25f, -1.0f, 0.5f) - (FullTurn - 0.25f)) < Margin);
        CHECK(std::abs(Wotah::TurnAngle(0.0f, FullTurn, 3.5f) - std::numbers::pi_v<float>) < 1.0e-4f);

        // Many small steps, as frames make them, stay within [0, 2 pi).
        float angle   = 0.0f;
        int   outside = 0;
        for (int frame = 0; frame < 100000; ++frame)
        {
            angle = Wotah::TurnAngle(angle, FullTurn / 10.0f, 1.0f / 60.0f);
            if (angle < 0.0f || angle >= FullTurn)
            {
                ++outside;
            }
        }
        CHECK(outside == 0);

        // A tiny step back from 0 rounds to a full turn, which comes out as 0.
        const float justBelow = Wotah::TurnAngle(0.0f, -1.0e-9f, 1.0f);
        CHECK(justBelow >= 0.0f);
        CHECK(justBelow < FullTurn);

        // A turn that is not a number leaves the angle alone.
        const float notANumber = std::numeric_limits<float>::quiet_NaN();
        const float endless    = std::numeric_limits<float>::infinity();
        CHECK(Wotah::TurnAngle(1.0f, notANumber, 1.0f) == 1.0f);
        CHECK(Wotah::TurnAngle(1.0f, 1.0f, endless) == 1.0f);
    }
}
