/**
 * @file logger_tests.cpp
 * Logger: the line format in wotah.log, and that lines dropped under load are reported. Each test closes the
 * program-wide log, works with a fresh one, and opens a fresh one again for the tests after it.
 */
#include "core/logger.hpp"
#include "core/utils.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace
{
    /** Every line of the log file next to the test program; empty when there is none. */
    std::vector<std::string> ReadLogFile()
    {
        const std::optional<std::filesystem::path> folder = Wotah::FindExecutableDirectory();
        REQUIRE(folder.has_value());

        std::vector<std::string> lines;
        std::ifstream            file(*folder / "wotah.log");
        for (std::string line; std::getline(file, line);)
        {
            lines.push_back(line);
        }
        return lines;
    }
} // namespace

TEST_SUITE("logger")
{
    TEST_CASE("lines are written as [hh:mm:ss][level] message")
    {
        Wotah::Logger::Destroy();
        Wotah::Logger::Create(); // overwrites the file, so it holds only the lines below
        Wotah::Logger::Info("hello {}", 42);
        Wotah::Logger::Warning("careful with {}", "water");
        Wotah::Logger::Destroy(); // writes everything out

        const std::vector<std::string> lines = ReadLogFile();
        Wotah::Logger::Create();

        REQUIRE(lines.size() == 2);
        CHECK(std::regex_match(lines[0], std::regex(R"(\[\d{2}:\d{2}:\d{2}\]\[info\] hello 42)")));
        CHECK(std::regex_match(lines[1], std::regex(R"(\[\d{2}:\d{2}:\d{2}\]\[warning\] careful with water)")));
    }

    TEST_CASE("logging never waits, and dropped lines are reported")
    {
        constexpr std::size_t LineCount = 200000; // far more than the queue holds (8192)

        Wotah::Logger::Destroy();
        Wotah::Logger::Create();
        for (std::size_t index = 0; index < LineCount; ++index)
        {
            Wotah::Logger::Info("line {}", index);
        }
        Wotah::Logger::Destroy();

        const std::vector<std::string> lines = ReadLogFile();
        Wotah::Logger::Create();

        // Either the writer kept up and every line is there, or the missing ones are reported at the end.
        const bool hasEveryLine = lines.size() >= LineCount;
        const bool reportsDrops = !lines.empty() && lines.back().find("log lines were dropped") != std::string::npos;
        CHECK((hasEveryLine || reportsDrops));
        CHECK(lines.size() <= LineCount + 1);
    }
}
