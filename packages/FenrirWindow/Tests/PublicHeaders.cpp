// FenrirWindow hides SDL (ADR-0020). This file includes the public headers before anything else, so if one of them ever
// reaches for SDL, or SDL's include path reaches a target outside FenrirWindow (this test executable is one), the build
// fails here.
#include "FenrirWindow/Window.hpp"
#include "FenrirWindow/WindowEvents.hpp"
#include "FenrirWindow/WindowTypes.hpp"

#if defined(SDL_MAJOR_VERSION) || defined(SDL_VERSION) || defined(SDL_INIT_VIDEO)
#error "A FenrirWindow public header includes SDL."
#endif

#if __has_include(<SDL3/SDL.h>)
#error "SDL's include path reaches a target outside FenrirWindow: it must be linked PRIVATE."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The public headers compile without SDL", "[window][seam]")
{
    const Fenrir::WindowConfig config;

    INFO("a window config made with no arguments is visible and stops the app when the main window closes");
    REQUIRE(config.visible);
    REQUIRE(config.stopOnMainWindowClose);
    INFO("the main window has an id before it exists");
    REQUIRE(Fenrir::Window::MainWindow() == Fenrir::WindowId{1});
}
