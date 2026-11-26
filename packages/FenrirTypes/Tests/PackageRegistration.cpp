// The test package's registration (Package/Generated/TestPackageTypes.*), which fenrir-describe-api and
// fenrir-generate-type-registry wrote from Package/TestPackage.hpp: data assets and a settings type visited by member,
// a base chain, a tagged union and every doc command, compiled against the header. The expected values are the
// header's.

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

TEST_CASE("The test package registers every type it describes")
{
    Registered package;

    INFO("diagnostics: " << (package.diagnostics.empty() ? std::string("none") : package.diagnostics.front()));
    REQUIRE(package.added);
    REQUIRE(package.diagnostics.empty());
    for (std::string_view name : {"Test::Layers", "Test::Rarity", "Test::Vec2", "Test::Marker", "Test::Options",
                                  "Test::Stat", "Test::WeaponData", "Test::Sword", "Test::Weather", "Test::Box",
                                  "Test::VolumeKind", "Test::Volume", "Test::Trigger"})
    {
        INFO(name << " should be registered under the test package");
        REQUIRE(package.Get(name).package == "TestPackage");
    }
}

TEST_CASE("Until a package defines them, the role hooks are no-ops that report success")
{
    TypeRegistry registry;
    const TypeDescription& marker = Fenrir::Describe<Test::Marker>::Description();
    const TypeDescription& weather = Fenrir::Describe<Test::Weather>::Description();
    const TypeDescription& layers = Fenrir::Describe<Test::Layers>::Description();

    INFO("the generated header's no-op hooks should return true and need no registered type");
    REQUIRE(Fenrir::AttachComponent<Test::Marker>(registry, marker));
    REQUIRE(Fenrir::AttachSceneResource<Test::Weather>(registry, weather));
    REQUIRE(Fenrir::AttachAppResource<Test::Layers>(registry, layers));
}

TEST_CASE("Describe<T> of a data asset is the description the registration added")
{
    Registered package;

    INFO("Describe<Test::Sword> should be the registered object, not a copy");
    REQUIRE(&Fenrir::Describe<Test::Sword>::Description() == package.registry.Find("Test::Sword"));
}

TEST_CASE("Describe<T>::role is the role of the description, known at compile time")
{
    static_assert(Fenrir::Describe<Test::Sword>::role == Fenrir::Role::DataAsset);
    static_assert(Fenrir::Describe<Test::Marker>::role == Fenrir::Role::Component);
    static_assert(Fenrir::Describe<Test::Rarity>::role == Fenrir::Role::Enum);

    INFO("Describe<Test::Options>::role should be the role its registered description has");
    REQUIRE(Fenrir::Describe<Test::Options>::role == Fenrir::Describe<Test::Options>::Description().role);
}

TEST_CASE("A data asset, a settings type and a by-member nested struct each have a visit function")
{
    Registered package;

    for (std::string_view name : {"Test::WeaponData", "Test::Sword", "Test::Options", "Test::Stat"})
    {
        INFO(name << " holds an owning kind, so should be visited by member through a visit function");
        REQUIRE(package.Get(name).visit != nullptr);
    }
    INFO("a fixed-layout type should be visited by its offsets, with no visit function");
    REQUIRE(package.Get("Test::Marker").visit == nullptr);
}

TEST_CASE("Visiting a Sword gives its base WeaponData's fields first, then its own")
{
    Registered package;
    Test::Sword sword;
    FieldsVisitor visitor;

    package.Get("Test::Sword").Visit(&sword, visitor);

    INFO("WeaponData's displayName, stats and damage should come before Sword's edge, icon and titles");
    REQUIRE(visitor.names == std::vector<std::string>{"displayName", "stats", "damage", "edge", "icon", "titles"});
}

TEST_CASE("A data asset's description names the data asset it extends")
{
    Registered package;

    INFO("Sword extends WeaponData, as its header says");
    REQUIRE(package.Get("Test::Sword").base == "Test::WeaponData");
    INFO("WeaponData extends nothing");
    REQUIRE(package.Get("Test::WeaponData").base.empty());
}

TEST_CASE("IsA answers over a data asset's base chain, by canonical or former name")
{
    Registered package;

    INFO("a Sword is a WeaponData");
    REQUIRE(package.registry.IsA("Test::Sword", "Test::WeaponData"));
    INFO("a type is itself");
    REQUIRE(package.registry.IsA("Test::Sword", "Test::Sword"));
    INFO("a WeaponData is not a Sword");
    REQUIRE_FALSE(package.registry.IsA("Test::WeaponData", "Test::Sword"));
    INFO("Sword's former name Test::Blade should answer as Sword does");
    REQUIRE(package.registry.IsA("Test::Blade", "Test::WeaponData"));
}

TEST_CASE("Registering Sword before WeaponData is refused with a diagnostic naming both")
{
    std::vector<std::string> diagnostics;
    TypeRegistry registry{[&](std::string_view message) { diagnostics.emplace_back(message); }};
    INFO("Stat, which WeaponData's list holds, should register alone");
    REQUIRE(registry.Register(Fenrir::Describe<Test::Stat>::Description()));

    const bool added = registry.Register(Fenrir::Describe<Test::Sword>::Description());

    INFO("Sword should be refused while its base is not registered");
    REQUIRE_FALSE(added);
    REQUIRE(registry.Find("Test::Sword") == nullptr);
    INFO("one diagnostic should name the type and its base: " << (diagnostics.empty() ? "" : diagnostics.front()));
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics.front().find("Test::Sword") != std::string::npos);
    REQUIRE(diagnostics.front().find("Test::WeaponData") != std::string::npos);
}

TEST_CASE("A list of nested structs is resized and filled through the visitor and read back from the struct")
{
    Registered package;
    Test::Sword sword;
    FieldsVisitor visitor([](const FieldInfo& field, ValueRef value) {
        if (field.name != "stats")
            return;
        INFO("stats should be a list");
        REQUIRE(value.GetKind() == Kind::List);
        value.Resize(2);
        for (std::size_t i = 0; i < value.Size(); ++i)
        {
            ValueRef stat = value.Element(i);
            INFO("each element should be a Stat, described by Stat's own description");
            REQUIRE(stat.Description() == &Fenrir::Describe<Test::Stat>::Description());
            FieldsVisitor statVisitor([i](const FieldInfo& statField, ValueRef statValue) {
                if (statField.name == "name")
                    statValue.SetText(i == 0 ? "weight" : "speed");
                else if (statField.name == "value")
                    *statValue.Get<float>() = static_cast<float>(i) + 0.5f;
            });
            stat.Visit(statVisitor);
            INFO("a Stat should be visited through its own visit function, name then value");
            REQUIRE(statVisitor.names == std::vector<std::string>{"name", "value"});
        }
    });

    package.Get("Test::Sword").Visit(&sword, visitor);

    INFO("the resize and the written elements should be the struct's own vector");
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
        INFO("edge should be an empty optional by default");
        REQUIRE(value.GetKind() == Kind::Optional);
        REQUIRE_FALSE(value.HasValue());
        *value.Emplace().Get<float>() = 0.75f;
    });
    swordType.Visit(&sword, emplace);
    INFO("the emplaced value should be the struct's optional's");
    REQUIRE(sword.edge == 0.75f);

    FieldsVisitor reset([](const FieldInfo& field, ValueRef value) {
        if (field.name == "edge")
            value.Reset();
    });
    swordType.Visit(&sword, reset);
    INFO("reset should empty the struct's optional");
    REQUIRE_FALSE(sword.edge.has_value());
}

TEST_CASE("A string, a path and a list of strings are written through the visitor, the base's string too")
{
    Registered package;
    Test::Sword sword;
    std::vector<std::string> read;
    FieldsVisitor visitor([&](const FieldInfo& field, ValueRef value) {
        if (field.name == "displayName" && value.GetKind() == Kind::String)
        {
            read.push_back(value.Text());
            value.SetText("Longsword");
        }
        else if (field.name == "icon" && value.GetKind() == Kind::Path)
        {
            read.push_back(value.Text());
            value.SetText("icons/longsword.png");
        }
        else if (field.name == "titles")
        {
            value.Resize(1);
            value.Element(0).SetText("Oathkeeper");
        }
    });

    package.Get("Test::Sword").Visit(&sword, visitor);

    INFO("the string and the path should read as their defaults, the header's literals");
    REQUIRE(read == std::vector<std::string>{"Weapon", "icons/sword.png"});
    INFO("the base's string, the path and the list of strings should hold what was written");
    REQUIRE(sword.displayName == "Longsword");
    REQUIRE(sword.icon == std::filesystem::path("icons/longsword.png"));
    REQUIRE(sword.titles == std::vector<std::string>{"Oathkeeper"});
}

TEST_CASE("A settings type is visited by member, its defaults the header's")
{
    Registered package;
    const TypeDescription& options = package.Get("Test::Options");
    INFO("Options is selected as a settings type");
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

    INFO("root, extensions and threads should read as assets, empty and 4");
    REQUIRE(seen == std::vector<std::string>{"assets", "0", "4"});
    options.Destroy(storage);
}

TEST_CASE("@transient and @editor-only are the types' flags")
{
    Registered package;

    INFO("Weather is marked @transient");
    REQUIRE(package.Get("Test::Weather").transient);
    REQUIRE_FALSE(package.Get("Test::Weather").editorOnly);
    INFO("Layers is marked @editor-only");
    REQUIRE(package.Get("Test::Layers").editorOnly);
    REQUIRE_FALSE(package.Get("Test::Layers").transient);
    INFO("Sword is marked neither");
    REQUIRE_FALSE(package.Get("Test::Sword").transient);
    REQUIRE_FALSE(package.Get("Test::Sword").editorOnly);
}

TEST_CASE("@version is a type's version, 1 when not given")
{
    Registered package;

    INFO("Marker is @version 3");
    REQUIRE(package.Get("Test::Marker").version == 3);
    INFO("Sword is @version 2");
    REQUIRE(package.Get("Test::Sword").version == 2);
    INFO("WeaponData gives no version");
    REQUIRE(package.Get("Test::WeaponData").version == 1);
}

TEST_CASE("Each @formerly name finds its type, and a field's are on its field")
{
    Registered package;

    for (std::string_view former : {"Test::Beacon", "Test::Flag"})
    {
        INFO(former << " is a former name of Marker");
        REQUIRE(package.registry.Find(former) == &package.Get("Test::Marker"));
    }
    INFO("Test::Blade is a former name of Sword");
    REQUIRE(package.registry.Find("Test::Blade") == &package.Get("Test::Sword"));
    const FieldInfo& target = FieldNamed(package.Get("Test::Marker"), "target");
    INFO("Marker's target was formerly pointee");
    REQUIRE(target.formerly.size() == 1);
    REQUIRE(target.formerly[0] == "pointee");
}

TEST_CASE("The component's fixed-layout kinds are an entity, a fixed string and a reference")
{
    Registered package;
    const TypeDescription& marker = package.Get("Test::Marker");

    INFO("target is a Fenrir::Entity");
    REQUIRE(FieldNamed(marker, "target").type.kind == Kind::Entity);
    INFO("label is a char[16]");
    REQUIRE(FieldNamed(marker, "label").type.kind == Kind::FixedString);
    REQUIRE(FieldNamed(marker, "label").type.count == 16);
    INFO("mesh is an AssetHandle<Test::Mesh>");
    REQUIRE(FieldNamed(marker, "mesh").type.kind == Kind::Reference);
    REQUIRE(FieldNamed(marker, "mesh").type.target == "Test::Mesh");
    INFO("offset is the bound type Vec2");
    REQUIRE(FieldNamed(marker, "offset").type.type == &package.Get("Test::Vec2"));
}

TEST_CASE("Every field metadata command's value is on its field")
{
    Registered package;
    const TypeDescription& marker = package.Get("Test::Marker");
    const FieldInfo& label = FieldNamed(marker, "label");
    const FieldInfo& offset = FieldNamed(marker, "offset");

    INFO("label has @display Caption and @multiline");
    REQUIRE(label.metadata.display == "Caption");
    REQUIRE(label.metadata.multiline);
    INFO("mesh has @group Look");
    REQUIRE(FieldNamed(marker, "mesh").metadata.group == "Look");
    INFO("offset has @range -10 10, @step 0.25 and @units m");
    REQUIRE(offset.metadata.range.has_value());
    REQUIRE(offset.metadata.range->min == -10.0);
    REQUIRE(offset.metadata.range->max == 10.0);
    REQUIRE(offset.metadata.step == 0.25);
    REQUIRE(offset.metadata.units == "m");
    INFO("rarity has @readonly and serial @hidden");
    REQUIRE(FieldNamed(marker, "rarity").metadata.readonly);
    REQUIRE(FieldNamed(marker, "serial").metadata.hidden);
    INFO("target has its doc text and no metadata command");
    REQUIRE(FieldNamed(marker, "target").doc == "The entity the marker points at.");
    REQUIRE_FALSE(FieldNamed(marker, "target").metadata.readonly);
    REQUIRE(FieldNamed(marker, "target").metadata.display.empty());
}

TEST_CASE("A component holding the test package's tagged union visits its tag, then its live box")
{
    Registered package;
    Test::Trigger trigger;
    std::vector<std::string> volume;
    FieldsVisitor visitor([&](const FieldInfo& field, ValueRef value) {
        if (field.name != "volume")
            return;
        FieldsVisitor inner;
        value.Visit(inner);
        volume = inner.names;
    });

    package.Get("Test::Trigger").Visit(&trigger, visitor);

    INFO("Trigger's volume is a struct of the tagged union Volume, whose default is a box");
    REQUIRE(visitor.names == std::vector<std::string>{"volume", "once"});
    REQUIRE(package.Get("Test::Volume").IsTaggedUnion());
    REQUIRE(volume == std::vector<std::string>{"m_kind", "m_box"});
}

TEST_CASE("Selecting the test package's scalar alternative writes its tag value and a zero radius")
{
    Registered package;
    const TypeDescription& description = package.Get("Test::Volume");
    Test::Volume volume(Test::Box{Test::Vec2(3.0f, 4.0f)});

    INFO("Circle is tag value 2, the second alternative, and selecting it value-initialises the float");
    REQUIRE(description.SelectAlternative(&volume, 2));
    REQUIRE(volume.Kind() == Test::VolumeKind::Circle);
    REQUIRE(volume.Radius() == 0.0f);

    FieldsVisitor visitor;
    description.Visit(&volume, visitor);
    INFO("the visit gives the tag, then the radius alone");
    REQUIRE(visitor.names == std::vector<std::string>{"m_kind", "m_radius"});
}

TEST_CASE("The test package's tagged union refuses a write of its tag through the visitor")
{
    Registered package;
    Test::Volume volume(1.5f);
    bool written = true;
    FieldsVisitor visitor([&](const FieldInfo& field, ValueRef value) {
        if (field.name == "m_kind")
            written = value.SetEnumValue(0);
    });

    package.Get("Test::Volume").Visit(&volume, visitor);

    INFO("the tag's write is refused, so the volume is still the circle it was");
    REQUIRE_FALSE(written);
    REQUIRE(volume.Kind() == Test::VolumeKind::Circle);
    REQUIRE(volume.Radius() == 1.5f);
}
