// FenrirECS sits below FenrirScene and FenrirApp, and above FenrirTypes and FenrirMath. This file includes its headers
// before anything else, so if one of them ever reaches for a package above it, or that package's include path reaches a
// user of FenrirECS (this test target is one), the build fails here.
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/ComponentWatch.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/DynamicComponent.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/Generated/FenrirECSTypes.hpp"
#include "FenrirECS/TrackedComponents.hpp"
#include "FenrirECS/TypeAttachments.hpp"
#include "FenrirECS/WorldTransform.hpp"

#if __has_include("FenrirScene/Scene.hpp") || __has_include("FenrirApp/App.hpp")
#error "FenrirScene or FenrirApp reaches a user of FenrirECS, which sits below them."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirECS's headers compile with no package above it", "[ecs][seam]")
{
    const Fenrir::TypeRegistry registry;

    INFO("a new type registry has no component to name");
    REQUIRE(!Fenrir::ComponentType::ForName(registry, "Fenrir::Transform").has_value());
}
