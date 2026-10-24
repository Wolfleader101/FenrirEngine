// Vsync through the graphics surface's swap interval.

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "GlTestApp.hpp"

using namespace Fenrir::Graphics;

namespace
{
    /** @brief The warnings about vsync among `warnings`, leaving out GL's own debug messages. */
    std::vector<std::string> VsyncWarnings(const std::vector<std::string>& warnings)
    {
        std::vector<std::string> found;
        std::ranges::copy_if(warnings, std::back_inserter(found),
                             [](const std::string& warning) { return warning.find("vsync") != std::string::npos; });
        return found;
    }
} // namespace

TEST_CASE("A vsync mode the surface supports is put into effect", "[renderer][gl]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();

    INFO("vsync On, which every surface supports, is the mode in effect");
    REQUIRE(test.device->SetVsync(Vsync::On) == Vsync::On);
    INFO("without a warning about it");
    REQUIRE(VsyncWarnings(test.messages->warnings).empty());
}

TEST_CASE("A vsync mode the surface does not support falls back to On, logged", "[renderer][gl]")
{
    // A surface refusing the adaptive swap interval, as a driver without it does.
    GlTest::GlTestApp test(-1);
    test.RequireOpenGL();

    const Vsync applied = test.device->SetVsync(Vsync::Adaptive);

    INFO("vsync falls back to On");
    REQUIRE(applied == Vsync::On);
    INFO("and the fallback is logged as a warning naming the refused mode");
    const std::vector<std::string> warnings = VsyncWarnings(test.messages->warnings);
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings.front().find("Adaptive") != std::string::npos);
}
