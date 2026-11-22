#include "TestTypes.hpp"

#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Describe<T> yields the description the registration added, by identity", "[types][describe]")
{
    Fenrir::TypeRegistry registry;
    INFO("setup: Transform, WeaponData and Sword register");
    REQUIRE(registry.Register(TypesTest::TransformDescription));
    REQUIRE(registry.Register(TypesTest::WeaponDataDescription));
    REQUIRE(registry.Register(TypesTest::SwordDescription));

    INFO("Describe<Transform> is the object the registry holds for its name");
    REQUIRE(&Fenrir::Describe<TypesTest::Transform>::Description() == registry.Find("TypesTest::Transform"));

    INFO("Describe<Sword> is the object the registry holds for its name");
    REQUIRE(&Fenrir::Describe<TypesTest::Sword>::Description() == registry.Find("TypesTest::Sword"));
}

TEST_CASE("Only a type something describes is Described", "[types][describe]")
{
    INFO("Transform has a Describe specialisation, so is Described");
    STATIC_REQUIRE(Fenrir::Described<TypesTest::Transform>);

    INFO("Gem has none, so is not");
    STATIC_REQUIRE_FALSE(Fenrir::Described<TypesTest::Gem>);
}
