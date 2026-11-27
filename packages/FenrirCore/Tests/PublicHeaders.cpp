// FenrirCore sits below every other package and links nothing. This file includes its headers before anything else,
// so if FenrirCore's include path ever reaches the packages built on it, the build fails here. That each header
// compiles alone is checked by the one-line sources Tests/CMakeLists.txt writes for them.
#include "FenrirCore/FenrirCore.hpp"
#include "FenrirCore/ReloadWatcher.hpp"

#if __has_include("FenrirApp/App.hpp") || __has_include("FenrirLogger/ILogger.hpp")
#error "FenrirCore's include path reaches FenrirApp or FenrirLogger, which are built on it."
#endif

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>

TEST_CASE("a default reload watcher from FenrirCore's header sees a new file as Settling", "[core][seam]")
{
    using namespace std::chrono_literals;
    Fenrir::ReloadWatcher watcher;
    const Fenrir::ReloadWatcher::Stamp stamp{std::filesystem::file_time_type{} + 1s, 1};

    // The default interval is 250 ms, so a look 250 ms after the clock's start is the first one.
    const auto now = Fenrir::ReloadWatcher::Clock::time_point{} + 250ms;
    const auto watch = watcher.Poll(now, [&] { return std::optional(stamp); }, [] { return true; });

    INFO("a file seen for the first time, with nothing loaded, should be a change still settling");
    REQUIRE(watch == Fenrir::ReloadWatcher::Watch::Settling);
}
