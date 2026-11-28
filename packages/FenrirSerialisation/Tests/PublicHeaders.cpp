// FenrirSerialisation links FenrirTypes and FenrirLogger and nothing else (ADR-0079 §1). This file includes its headers
// before anything else, so if one of them ever reaches for a package above it, the build fails here. That each header
// also compiles alone is checked by the one-line sources the test CMakeLists.txt writes per header.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/HeldFields.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"

#if __has_include("FenrirAssets/AssetHandle.hpp") || __has_include("FenrirScene/Scene.hpp")
#error "FenrirAssets or FenrirScene reaches a user of FenrirSerialisation, which knows no assets or scenes."
#endif

#if __has_include("FenrirECS/Entity.hpp") || __has_include("FenrirMath/Math.hpp")
#error "FenrirECS or FenrirMath reaches a user of FenrirSerialisation, which links FenrirTypes and FenrirLogger alone."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirSerialisation's headers compile with no package above them", "[serialisation][seam]")
{
    const Fenrir::Serialisation::Value value;

    INFO("a value built with no arguments is a null");
    REQUIRE(value.Kind() == Fenrir::Serialisation::ValueKind::Null);
}
