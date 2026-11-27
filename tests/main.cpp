/**
 * @file main.cpp
 * The test program's entry point: runs every doctest test case with the log running, as in the real program.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include "core/logger.hpp"

#include <doctest/doctest.h>

int main(int argc, char** argv)
{
    Wotah::Logger::Create();

    doctest::Context context(argc, argv);
    const int        result = context.run();

    Wotah::Logger::Destroy();
    return result;
}
