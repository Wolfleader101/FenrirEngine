// The test package's registration (Package/Generated/TestPackageTypes.*), which fenrir-describe-api and
// fenrir-generate-type-registry wrote from Package/TestPackage.hpp: data assets and a settings type visited by member,
// a base chain, and every doc command, compiled against the header. The expected values are the header's.

#include "Generated/TestPackageTypes.hpp"

#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Fenrir::FieldInfo;
using Fenrir::Kind;
using Fenrir::TypeDescription;
using Fenrir::TypeRegistry;
using Fenrir::ValueRef;

namespace
{
    // A registry holding the test package's registration, and every diagnostic it reported.
    struct Registered
    {
        std::vector<std::string> diagnostics;
        TypeRegistry registry{[this](std::string_view message) { diagnostics.emplace_back(message); }};
        bool added = Fenrir::Generated::RegisterTestPackageTypes(registry);

        // The type registered under `name`, which the registration must have added.
        const TypeDescription& Get(std::string_view name) const
        {
            const TypeDescription* description = registry.Find(name);
            INFO(name << " should be registered");
            REQUIRE(description != nullptr);
            return *description;
        }
    };

    // Hands each field it is given to `onField`, recording the fields' names in the order they came.
    class FieldsVisitor final : public Fenrir::FieldVisitor
    {
      public:
        explicit FieldsVisitor(std::function<void(const FieldInfo&, ValueRef)> onField = {})
            : m_onField(std::move(onField))
        {
        }

        std::vector<std::string> names;

        void Field(const FieldInfo& field, ValueRef value) override
        {
            names.emplace_back(field.name);
            if (m_onField)
                m_onField(field, value);
        }

      private:
        std::function<void(const FieldInfo&, ValueRef)> m_onField;
    };

    // The field of `description` named `name`, which it must declare.
    const FieldInfo& FieldNamed(const TypeDescription& description, std::string_view name)
    {
        for (const FieldInfo& field : description.fields)
            if (field.name == name)
                return field;
        throw std::logic_error("no such field");
    }
} // namespace

TEST_CASE("The test package registers every type, each data asset after its base, the hooks no-ops")
{
    Registered package;

    INFO("diagnostics: " << (package.diagnostics.empty() ? std::string("none") : package.diagnostics.front()));
    REQUIRE(package.added);
    REQUIRE(package.diagnostics.empty());
    for (std::string_view name : {"Test::Layers", "Test::Rarity", "Test::Vec2", "Test::Marker", "Test::Options",
                                  "Test::Stat", "Test::WeaponData", "Test::Sword", "Test::Weather"})
    {
        INFO(name);
        REQUIRE(package.Get(name).package == "TestPackage");
    }
    REQUIRE(&Fenrir::Describe<Test::Sword>::Description() == package.registry.Find("Test::Sword"));
}

TEST_CASE("A data asset, a settings type and a by-member nested struct each have a visit function")
{
    Registered package;

    for (std::string_view name : {"Test::WeaponData", "Test::Sword", "Test::Options", "Test::Stat"})
    {
        INFO(name << " holds an owning kind, so is visited by member");
        REQUIRE(package.Get(name).visit != nullptr);
    }
    INFO("a fixed-layout type is visited by its offsets");
    REQUIRE(package.Get("Test::Marker").visit == nullptr);
}

TEST_CASE("Visiting a Sword gives its base WeaponData's fields first, then its own")
{
    Registered package;
    Test::Sword sword;
    FieldsVisitor visitor;

    package.Get("Test::Sword").Visit(&sword, visitor);

    REQUIRE(visitor.names == std::vector<std::string>{"displayName", "stats", "damage", "edge", "icon", "titles"});
}

TEST_CASE("A data asset's base is recorded, and IsA answers over the chain")
{
    Registered package;

    REQUIRE(package.Get("Test::Sword").base == "Test::WeaponData");
    REQUIRE(package.Get("Test::WeaponData").base.empty());
    REQUIRE(package.registry.IsA("Test::Sword", "Test::WeaponData"));
    REQUIRE(package.registry.IsA("Test::Sword", "Test::Sword"));
    REQUIRE_FALSE(package.registry.IsA("Test::WeaponData", "Test::Sword"));
    INFO("a former name of the derived type answers the same");
    REQUIRE(package.registry.IsA("Test::Blade", "Test::WeaponData"));
}

TEST_CASE("Registering Sword before WeaponData is refused with a diagnostic naming both")
{
    std::vector<std::string> diagnostics;
    TypeRegistry registry{[&](std::string_view message) { diagnostics.emplace_back(message); }};

    REQUIRE(registry.Register(Fenrir::Describe<Test::Stat>::Description()));
    REQUIRE_FALSE(registry.Register(Fenrir::Describe<Test::Sword>::Description()));

    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics.front().find("Test::Sword") != std::string::npos);
    REQUIRE(diagnostics.front().find("Test::WeaponData") != std::string::npos);
    REQUIRE(registry.Find("Test::Sword") == nullptr);
}

TEST_CASE("A list of nested structs is resized and filled through the visitor and read back from the struct")
{
    Registered package;
    Test::Sword sword;
    FieldsVisitor visitor([](const FieldInfo& field, ValueRef value) {
        if (field.name != "stats")
            return;
        REQUIRE(value.GetKind() == Kind::List);
        value.Resize(2);
        for (std::size_t i = 0; i < value.Size(); ++i)
        {
            ValueRef stat = value.Element(i);
            REQUIRE(stat.Description() == &Fenrir::Describe<Test::Stat>::Description());
            FieldsVisitor statVisitor([i](const FieldInfo& statField, ValueRef statValue) {
                if (statField.name == "name")
                    statValue.SetText(i == 0 ? "weight" : "speed");
                else if (statField.name == "value")
                    *statValue.Get<float>() = static_cast<float>(i) + 0.5f;
            });
            stat.Visit(statVisitor);
            REQUIRE(statVisitor.names == std::vector<std::string>{"name", "value"});
        }
    });

    package.Get("Test::Sword").Visit(&sword, visitor);

    REQUIRE(sword.stats.size() == 2);
    REQUIRE(sword.stats[0].name == "weight");
    REQUIRE(sword.stats[0].value == 0.5f);
    REQUIRE(sword.stats[1].name == "speed");
    REQUIRE(sword.stats[1].value == 1.5f);
}

TEST_CASE("An optional is emplaced and cleared through the visitor")
{
    Registered package;
    const TypeDescription& swordType = package.Get("Test::Sword");
    Test::Sword sword;

    FieldsVisitor emplace([](const FieldInfo& field, ValueRef value) {
        if (field.name != "edge")
            return;
        REQUIRE(value.GetKind() == Kind::Optional);
        REQUIRE_FALSE(value.HasValue());
        *value.Emplace().Get<float>() = 0.75f;
    });
    swordType.Visit(&sword, emplace);
    REQUIRE(sword.edge == 0.75f);

    FieldsVisitor reset([](const FieldInfo& field, ValueRef value) {
        if (field.name == "edge")
            value.Reset();
    });
    swordType.Visit(&sword, reset);
    REQUIRE_FALSE(sword.edge.has_value());
}

TEST_CASE("A string, a path and a list of strings are written through the visitor, the base's string too")
{
    Registered package;
    Test::Sword sword;
    FieldsVisitor visitor([](const FieldInfo& field, ValueRef value) {
        if (field.name == "displayName")
        {
            REQUIRE(value.GetKind() == Kind::String);
            REQUIRE(value.Text() == "Weapon");
            value.SetText("Longsword");
        }
        else if (field.name == "icon")
        {
            REQUIRE(value.GetKind() == Kind::Path);
            REQUIRE(value.Text() == "icons/sword.png");
            value.SetText("icons/longsword.png");
        }
        else if (field.name == "titles")
        {
            value.Resize(1);
            value.Element(0).SetText("Oathkeeper");
        }
    });

    package.Get("Test::Sword").Visit(&sword, visitor);

    REQUIRE(sword.displayName == "Longsword");
    REQUIRE(sword.icon == std::filesystem::path("icons/longsword.png"));
    REQUIRE(sword.titles == std::vector<std::string>{"Oathkeeper"});
}

TEST_CASE("A settings type is visited by member, its defaults the header's")
{
    Registered package;
    const TypeDescription& options = package.Get("Test::Options");
    REQUIRE(options.role == Fenrir::Role::Settings);
    alignas(Test::Options) std::byte storage[sizeof(Test::Options)];
    options.Construct(storage);
    std::vector<std::string> seen;
    FieldsVisitor visitor([&](const FieldInfo& field, ValueRef value) {
        if (field.name == "root")
            seen.push_back(value.Text());
        else if (field.name == "extensions")
            seen.push_back(std::to_string(value.Size()));
        else if (field.name == "threads")
            seen.push_back(std::to_string(*value.Get<std::uint32_t>()));
    });

    options.Visit(storage, visitor);

    REQUIRE(seen == std::vector<std::string>{"assets", "0", "4"});
    options.Destroy(storage);
}

TEST_CASE("@transient and @editor-only are the types' flags")
{
    Registered package;

    REQUIRE(package.Get("Test::Weather").transient);
    REQUIRE_FALSE(package.Get("Test::Weather").editorOnly);
    REQUIRE(package.Get("Test::Layers").editorOnly);
    REQUIRE_FALSE(package.Get("Test::Layers").transient);
    REQUIRE_FALSE(package.Get("Test::Sword").transient);
}

TEST_CASE("@version and @formerly are the types' version and former names")
{
    Registered package;

    REQUIRE(package.Get("Test::Marker").version == 3);
    REQUIRE(package.Get("Test::Sword").version == 2);
    REQUIRE(package.Get("Test::WeaponData").version == 1);
    for (std::string_view former : {"Test::Beacon", "Test::Flag"})
    {
        INFO(former);
        REQUIRE(package.registry.Find(former) == &package.Get("Test::Marker"));
    }
    REQUIRE(package.registry.Find("Test::Blade") == &package.Get("Test::Sword"));
    REQUIRE(FieldNamed(package.Get("Test::Marker"), "target").formerly.size() == 1);
    REQUIRE(FieldNamed(package.Get("Test::Marker"), "target").formerly[0] == "pointee");
}

TEST_CASE("Every field metadata command's value is on its field")
{
    Registered package;
    const TypeDescription& marker = package.Get("Test::Marker");

    const FieldInfo& label = FieldNamed(marker, "label");
    REQUIRE(label.type.kind == Kind::FixedString);
    REQUIRE(label.type.count == 16);
    REQUIRE(label.metadata.display == "Caption");
    REQUIRE(label.metadata.multiline);

    const FieldInfo& mesh = FieldNamed(marker, "mesh");
    REQUIRE(mesh.type.kind == Kind::Reference);
    REQUIRE(mesh.type.target == "Test::Mesh");
    REQUIRE(mesh.metadata.group == "Look");

    const FieldInfo& offset = FieldNamed(marker, "offset");
    REQUIRE(offset.type.type == &package.Get("Test::Vec2"));
    REQUIRE(offset.metadata.range.has_value());
    REQUIRE(offset.metadata.range->min == -10.0);
    REQUIRE(offset.metadata.range->max == 10.0);
    REQUIRE(offset.metadata.step == 0.25);
    REQUIRE(offset.metadata.units == "m");

    REQUIRE(FieldNamed(marker, "rarity").metadata.readonly);
    REQUIRE(FieldNamed(marker, "serial").metadata.hidden);
    REQUIRE(FieldNamed(marker, "target").type.kind == Kind::Entity);
    REQUIRE(FieldNamed(marker, "target").doc == "The entity the marker points at.");
    INFO("a field with no metadata command has none");
    REQUIRE_FALSE(FieldNamed(marker, "target").metadata.readonly);
    REQUIRE(FieldNamed(marker, "target").metadata.display.empty());
}
