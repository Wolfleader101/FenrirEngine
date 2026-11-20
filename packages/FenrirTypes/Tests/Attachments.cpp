#include "TestTypes.hpp"

#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using Fenrir::TypeRegistry;

namespace
{
    // Two tags with the same value type, standing in for two packages' facts about a type.
    struct SizeTag
    {
        using Value = int;
    };

    struct OtherSizeTag
    {
        using Value = int;
    };

    // A tag whose value can only be moved, as an upgrader list holding callables might be.
    struct UpgradersTag
    {
        using Value = std::unique_ptr<std::vector<std::string>>;
    };
} // namespace

TEST_CASE("An attachment hung on a type reads back", "[types][attachments]")
{
    TypeRegistry registry;
    REQUIRE(registry.Register(TypesTest::TransformDescription));

    INFO("hanging a value on a registered type succeeds");
    REQUIRE(registry.Attach<SizeTag>("TypesTest::Transform", 40));

    const TypeRegistry& readOnly = registry;
    const int* size = readOnly.Attachment<SizeTag>("TypesTest::Transform");
    INFO("the value reads back under its tag");
    REQUIRE(size != nullptr);
    REQUIRE(*size == 40);

    INFO("a former name reaches the same attachment");
    REQUIRE(readOnly.Attachment<SizeTag>("TypesTest::Placement") == size);
}

TEST_CASE("A missing attachment reads as absent, not as an error", "[types][attachments]")
{
    std::vector<std::string> diagnostics;
    TypeRegistry registry([&](std::string_view message) { diagnostics.emplace_back(message); });
    REQUIRE(registry.Register(TypesTest::TransformDescription));
    REQUIRE(registry.Attach<SizeTag>("TypesTest::Transform", 40));

    INFO("another tag with the same value type is a different attachment, absent here");
    REQUIRE(registry.Attachment<OtherSizeTag>("TypesTest::Transform") == nullptr);

    INFO("an unregistered type has no attachments");
    REQUIRE(registry.Attachment<SizeTag>("TypesTest::Nothing") == nullptr);

    INFO("reading absent attachments reports nothing");
    REQUIRE(diagnostics.empty());
}

TEST_CASE("Attaching again under one tag replaces the value", "[types][attachments]")
{
    TypeRegistry registry;
    REQUIRE(registry.Register(TypesTest::TransformDescription));
    REQUIRE(registry.Attach<SizeTag>("TypesTest::Transform", 40));
    REQUIRE(registry.Attach<SizeTag>("TypesTest::Transform", 41));

    INFO("the second value replaced the first");
    REQUIRE(*registry.Attachment<SizeTag>("TypesTest::Transform") == 41);
}

TEST_CASE("An attachment is changed in place through the mutable registry", "[types][attachments]")
{
    TypeRegistry registry;
    REQUIRE(registry.Register(TypesTest::WeaponDataDescription));
    REQUIRE(registry.Attach<UpgradersTag>("TypesTest::WeaponData", std::make_unique<std::vector<std::string>>()));

    (*registry.Attachment<UpgradersTag>("TypesTest::WeaponData"))->emplace_back("1 to 2");

    INFO("a move-only value is held, and what was added to it through the registry stays");
    REQUIRE((*registry.Attachment<UpgradersTag>("TypesTest::WeaponData"))->size() == 1);
}

TEST_CASE("Attaching to a type nothing registered is refused with a diagnostic", "[types][attachments]")
{
    std::vector<std::string> diagnostics;
    TypeRegistry registry([&](std::string_view message) { diagnostics.emplace_back(message); });

    INFO("attaching to an unregistered name fails");
    REQUIRE_FALSE(registry.Attach<SizeTag>("TypesTest::Nothing", 1));

    INFO("the diagnostic names the type: " << (diagnostics.empty() ? std::string() : diagnostics.front()));
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics.front().find("TypesTest::Nothing") != std::string::npos);
}
