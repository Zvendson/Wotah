/**
 * @file utils_tests.cpp
 * The helpers in core/utils.hpp.
 */
#include "core/utils.hpp"

#include <doctest/doctest.h>

#include <filesystem>
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
}
