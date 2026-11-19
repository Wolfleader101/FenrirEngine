// FenrirTypes sits below every other package and links nothing but the standard library (ADR-0074). This file includes
// its headers before anything else, so if one of them ever reaches for another engine package, or another package's
// include path reaches a user of FenrirTypes (this test target is one), the build fails here. That each header also
// compiles alone is checked by the one-line sources the test CMakeLists.txt writes per header.
#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#if __has_include("FenrirMath/Math.hpp") || __has_include("FenrirECS/Entity.hpp")
#error "FenrirMath or FenrirECS reaches a user of FenrirTypes, which sits below them."
#endif

#if __has_include("FenrirCore/FenrirCore.hpp") || __has_include("FenrirLogger/ILogger.hpp")
#error "FenrirCore or FenrirLogger reaches a user of FenrirTypes: its diagnostics go to a function the caller gives."
#endif

#if __has_include("FenrirAssets/AssetHandle.hpp")
#error "FenrirAssets reaches a user of FenrirTypes: it knows a reference's layout without naming the asset store."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirTypes's headers compile with no other engine package", "[types][seam]")
{
    const Fenrir::TypeRegistry registry;

    INFO("a new type registry knows no type");
    REQUIRE(registry.Find("Fenrir::Transform") == nullptr);
}
