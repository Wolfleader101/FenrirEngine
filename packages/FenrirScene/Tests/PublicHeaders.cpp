// FenrirScene sits below FenrirApp, above FenrirECS, and links FenrirTypes only for its resources' named doors. This
// file includes its header before anything else, so if it ever reaches for FenrirApp, or FenrirApp's include path
// reaches a user of FenrirScene (this test target is one), the build fails here.
#include "FenrirScene/Scene.hpp"

#if __has_include("FenrirApp/App.hpp") || __has_include("FenrirScheduler/Scheduler.hpp")
#error "FenrirApp or its scheduler reaches a user of FenrirScene, which sits below them."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirScene's header compiles with no package above it", "[scene][seam]")
{
    const Fenrir::TypeRegistry registry;
    Fenrir::Scene scene("Test");

    INFO("a new registry opens no resource door");
    REQUIRE(scene.FindResource(registry, "Fenrir::Sky") == nullptr);
}
