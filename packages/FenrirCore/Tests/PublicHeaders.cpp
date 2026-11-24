// FenrirCore sits below every other package and links nothing. This file includes each of its headers before anything
// else, so if one ever needs a header it does not include itself, or reaches for a package above it, the build fails
// here.
#include "FenrirCore/FenrirCore.hpp"
#include "FenrirCore/ReloadWatcher.hpp"

#if __has_include("FenrirApp/App.hpp") || __has_include("FenrirLogger/ILogger.hpp")
#error "A package above FenrirCore reaches a user of FenrirCore, which sits below them all."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirCore's headers compile with no package above them", "[core][seam]")
{
    const Fenrir::ReloadWatcher watcher;

    INFO("a default watcher can be made from the public header alone");
    REQUIRE(sizeof(watcher) > 0);
}
