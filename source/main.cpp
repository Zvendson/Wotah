/**
 * @file main.cpp
 * The entry point: runs the application through its steps.
 */
#include "app.hpp"

int main()
{
    Wotah::App app;
    if (!app.Create())
    {
        return 1;
    }

    const bool ranCleanly = app.Run();
    app.Destroy();

    return ranCleanly ? 0 : 1;
}
