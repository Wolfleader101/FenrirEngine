#include "RecordingRegistry.hpp"
#include "TestTypes.hpp"

#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using Fenrir::FieldInfo;
using Fenrir::Kind;
using Fenrir::Role;
using Fenrir::TypeDescription;
using Fenrir::TypeRegistry;
using Fenrir::ValueType;

using TypesTest::Contains;
using TypesTest::RecordingRegistry;

namespace
{
    // What a registrant such as FenrirScripting builds at run time and keeps alive while the type is registered: a
    // tree-backed data asset type extending the C++ `WeaponData`, with a string, a list and a reference field. Its
    // default tree's bytes stand in for an encoded tree, which FenrirTypes never reads.
    struct TreeBackedTypes
    {
        ValueType i32{.kind = Kind::I32};
        std::array<FieldInfo, 3> fields{{
            {.name = "title", .type = {.kind = Kind::String}},
            {.name = "combo", .type = {.kind = Kind::List, .element = &i32}},
            {.name = "upgrade", .type = {.kind = Kind::Reference, .target = "TypesTest::WeaponData"}},
        }};
        std::array<std::byte, 3> defaultTree{std::byte{1}, std::byte{2}, std::byte{3}};
        TypeDescription description{
            .name = "Game::ScriptedSword",
            .package = "GameScripts",
            .role = Role::DataAsset,
            .size = 0,
            .base = "TypesTest::WeaponData",
            .fields = fields,
            .treeBacked = true,
            .defaultTree = defaultTree,
        };
    };
} // namespace

TEST_CASE("A tree-backed type with string, list and reference fields registers and is found", "[types][tree-backed]")
{
    RecordingRegistry recording;
    INFO("setup: the C++ base WeaponData registers");
    REQUIRE(recording.registry.Register(TypesTest::WeaponDataDescription));
    const TreeBackedTypes types;

    INFO("a tree-backed type with owning fields and no visit function registers; diagnostics: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.front()));
    REQUIRE(recording.registry.Register(types.description));

    INFO("its name finds the registrant's own description, not a copy");
    REQUIRE(recording.registry.Find("Game::ScriptedSword") == &types.description);

    INFO("it is listed among the data asset types, after WeaponData");
    const std::vector<const TypeDescription*> assets = recording.registry.Types(Role::DataAsset);
    REQUIRE(assets.size() == 2);
    REQUIRE(assets[1] == &types.description);
}

TEST_CASE("A tree-backed type is-a the C++ data asset type it extends", "[types][tree-backed]")
{
    TypeRegistry registry;
    INFO("setup: WeaponData and a tree-backed type extending it register");
    REQUIRE(registry.Register(TypesTest::WeaponDataDescription));
    const TreeBackedTypes types;
    REQUIRE(registry.Register(types.description));

    INFO("it is a WeaponData, over its C++ base");
    REQUIRE(registry.IsA("Game::ScriptedSword", "TypesTest::WeaponData"));

    INFO("the C++ base is not one of it");
    REQUIRE_FALSE(registry.IsA("TypesTest::WeaponData", "Game::ScriptedSword"));
}

TEST_CASE("A tree-backed type extends another tree-backed type", "[types][tree-backed]")
{
    TypeRegistry registry;
    INFO("setup: WeaponData and a tree-backed type extending it register");
    REQUIRE(registry.Register(TypesTest::WeaponDataDescription));
    const TreeBackedTypes types;
    REQUIRE(registry.Register(types.description));

    TypeDescription longer = types.description;
    longer.name = "Game::LongSword";
    longer.base = "Game::ScriptedSword";
    longer.fields = {};

    INFO("a tree-backed type extending a tree-backed one registers");
    REQUIRE(registry.Register(longer));

    INFO("it is-a both types along its chain");
    REQUIRE(registry.IsA("Game::LongSword", "Game::ScriptedSword"));
    REQUIRE(registry.IsA("Game::LongSword", "TypesTest::WeaponData"));
}

TEST_CASE("A tree-backed type with a size or a visit function is refused", "[types][tree-backed]")
{
    RecordingRegistry recording;
    INFO("setup: WeaponData registers");
    REQUIRE(recording.registry.Register(TypesTest::WeaponDataDescription));
    const TreeBackedTypes types;

    TypeDescription sized = types.description;
    sized.size = 16;
    INFO("a tree-backed type with a size is refused");
    REQUIRE_FALSE(recording.registry.Register(sized));
    INFO("one diagnostic says it should have no layout: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.back()));
    REQUIRE(recording.diagnostics.size() == 1);
    REQUIRE(Contains(recording.diagnostics.back(), "no layout"));

    TypeDescription visited = types.description;
    visited.visit = TypesTest::VisitWeaponData;
    INFO("a tree-backed type with a visit function is refused");
    REQUIRE_FALSE(recording.registry.Register(visited));

    INFO("neither was added");
    REQUIRE(recording.registry.Find("Game::ScriptedSword") == nullptr);
}

TEST_CASE("A tree-backed type whose field has an offset is refused, naming the field", "[types][tree-backed]")
{
    RecordingRegistry recording;
    INFO("setup: WeaponData registers");
    REQUIRE(recording.registry.Register(TypesTest::WeaponDataDescription));
    const TreeBackedTypes types;
    std::array<FieldInfo, 1> offsetFields{{{.name = "title", .type = {.kind = Kind::String}, .offset = 8}}};
    TypeDescription offset = types.description;
    offset.fields = offsetFields;

    INFO("the type is refused");
    REQUIRE_FALSE(recording.registry.Register(offset));
    INFO("the diagnostic names the field: " << (recording.diagnostics.empty() ? std::string("none")
                                                                              : recording.diagnostics.back()));
    REQUIRE(Contains(recording.diagnostics.back(), "`title`"));
}

TEST_CASE("A tree-backed type with no default tree is refused", "[types][tree-backed]")
{
    RecordingRegistry recording;
    INFO("setup: WeaponData registers");
    REQUIRE(recording.registry.Register(TypesTest::WeaponDataDescription));
    const TreeBackedTypes types;
    TypeDescription noDefault = types.description;
    noDefault.defaultTree = {};

    INFO("the type is refused");
    REQUIRE_FALSE(recording.registry.Register(noDefault));
    INFO("the diagnostic says it has no default tree: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.back()));
    REQUIRE(Contains(recording.diagnostics.back(), "no default tree"));
}

TEST_CASE("A type that is not tree-backed may not carry a default tree", "[types][tree-backed]")
{
    RecordingRegistry recording;
    const TreeBackedTypes types;
    TypeDescription laidOut = TypesTest::WeaponDataDescription;
    laidOut.defaultTree = types.defaultTree;

    INFO("a C++ data asset with a default tree is refused");
    REQUIRE_FALSE(recording.registry.Register(laidOut));
    INFO("the diagnostic says it is not tree-backed: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.back()));
    REQUIRE(Contains(recording.diagnostics.back(), "not tree-backed"));
}

TEST_CASE("Only a data asset or nested type may be tree-backed", "[types][tree-backed]")
{
    RecordingRegistry recording;
    const TreeBackedTypes types;
    TypeDescription component = types.description;
    component.role = Role::Component;
    component.base = {};
    component.fields = {};

    INFO("a tree-backed component is refused");
    REQUIRE_FALSE(recording.registry.Register(component));
    INFO("the diagnostic names the roles that may be tree-backed: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.back()));
    REQUIRE(Contains(recording.diagnostics.back(), "only a data asset or nested type may be tree-backed"));
}

TEST_CASE("A C++ type may not extend a tree-backed type", "[types][tree-backed]")
{
    RecordingRegistry recording;
    INFO("setup: WeaponData and a tree-backed type extending it register");
    REQUIRE(recording.registry.Register(TypesTest::WeaponDataDescription));
    const TreeBackedTypes types;
    REQUIRE(recording.registry.Register(types.description));

    TypeDescription laidOut = TypesTest::SwordDescription;
    laidOut.name = "TypesTest::LaidOutSword";
    laidOut.base = "Game::ScriptedSword";

    INFO("a type with a layout extending a tree-backed one is refused");
    REQUIRE_FALSE(recording.registry.Register(laidOut));
    INFO("the diagnostic says its base is tree-backed: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.back()));
    REQUIRE(Contains(recording.diagnostics.back(), "is tree-backed"));
}

TEST_CASE("A C++ type may not hold a tree-backed struct, even in a list", "[types][tree-backed]")
{
    RecordingRegistry recording;
    TreeBackedTypes types;
    types.description.name = "Game::Charm";
    types.description.role = Role::Nested;
    types.description.base = {};
    INFO("setup: a tree-backed nested type registers");
    REQUIRE(recording.registry.Register(types.description));

    const ValueType charm{.kind = Kind::Struct, .type = &types.description};
    std::array<FieldInfo, 1> held{{{.name = "charms", .type = {.kind = Kind::List, .element = &charm}}}};
    TypeDescription laidOut = TypesTest::WeaponDataDescription;
    laidOut.name = "TypesTest::CharmedWeapon";
    laidOut.fields = held;

    INFO("a type with a layout holding a list of a tree-backed struct is refused");
    REQUIRE_FALSE(recording.registry.Register(laidOut));
    INFO("the diagnostic names the field and the held type: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.back()));
    REQUIRE(Contains(recording.diagnostics.back(), "`charms`"));
    REQUIRE(Contains(recording.diagnostics.back(), "`Game::Charm`"));

    TypeDescription treeHolder = types.description;
    treeHolder.name = "Game::CharmBag";
    treeHolder.role = Role::DataAsset;
    treeHolder.fields = held;
    INFO("a tree-backed type may hold one");
    REQUIRE(recording.registry.Register(treeHolder));
}

TEST_CASE("A C++ type holding a list of itself registers", "[types][tree-backed]")
{
    // The walk for held tree-backed structs goes through lists, and a list of the type itself is a legal member,
    // as `std::vector<Self>` is. Its visit function is never called here.
    TypeDescription node = TypesTest::WeaponDataDescription;
    node.name = "TypesTest::Node";
    const ValueType nodeType{.kind = Kind::Struct, .type = &node};
    const ValueType children{.kind = Kind::List, .element = &nodeType};
    std::array<FieldInfo, 1> fields{{{.name = "children", .type = children}}};
    node.fields = fields;

    RecordingRegistry recording;
    INFO("the type registers, the walk ending at the struct it already walked; diagnostics: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.front()));
    REQUIRE(recording.registry.Register(node));
}
