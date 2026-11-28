#include "RecordingRegistry.hpp"
#include "TestTypes.hpp"

#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

using Fenrir::Role;
using Fenrir::TypeDescription;
using Fenrir::TypeRegistry;

using TypesTest::Contains;
using TypesTest::RecordingRegistry;

TEST_CASE("A registered type is found by its canonical name", "[types][registry]")
{
    RecordingRegistry recording;

    INFO("registering a description with a new name succeeds");
    REQUIRE(recording.registry.Register(TypesTest::TransformDescription));

    INFO("the canonical name finds the registered description itself");
    REQUIRE(recording.registry.Find("TypesTest::Transform") == &TypesTest::TransformDescription);

    INFO("a name nothing registered finds nothing");
    REQUIRE(recording.registry.Find("TypesTest::Nothing") == nullptr);

    INFO("nothing was reported");
    REQUIRE(recording.diagnostics.empty());
}

TEST_CASE("A former name finds the type and reports its current name", "[types][registry]")
{
    TypeRegistry registry;
    INFO("setup: Transform registers");
    REQUIRE(registry.Register(TypesTest::TransformDescription));

    for (std::string_view former : {"TypesTest::Placement", "TypesTest::Pose"})
    {
        const TypeDescription* found = registry.Find(former);

        INFO("the former name " << former << " finds the type");
        REQUIRE(found == &TypesTest::TransformDescription);

        INFO("what it finds is named by the current canonical name");
        REQUIRE(found->name == "TypesTest::Transform");
    }
}

TEST_CASE("Registering a second type under one canonical name is refused, naming both, and the first stays",
          "[types][registry]")
{
    RecordingRegistry recording;
    INFO("setup: Transform registers");
    REQUIRE(recording.registry.Register(TypesTest::TransformDescription));

    const TypeDescription impostor{
        .name = "TypesTest::Transform",
        .package = "OtherPackage",
        .role = Role::SharedStruct,
        .size = 4,
        .alignment = 4,
    };

    INFO("the second registration under the same name fails");
    REQUIRE_FALSE(recording.registry.Register(impostor));

    INFO("one diagnostic is reported");
    REQUIRE(recording.diagnostics.size() == 1);

    const std::string& message = recording.diagnostics.front();
    INFO("the diagnostic names the name and both packages: " << message);
    REQUIRE(Contains(message, "TypesTest::Transform"));
    REQUIRE(Contains(message, "FenrirTypesTests"));
    REQUIRE(Contains(message, "OtherPackage"));

    INFO("the first registration stays");
    REQUIRE(recording.registry.Find("TypesTest::Transform") == &TypesTest::TransformDescription);
}

TEST_CASE("A type whose former name is another type's name is refused, and adds none of its names", "[types][registry]")
{
    RecordingRegistry recording;
    INFO("setup: Vec3 registers");
    REQUIRE(recording.registry.Register(TypesTest::Vec3Description));

    static constexpr std::string_view formerly[] = {"TypesTest::Vec3"};
    const TypeDescription renamed{
        .name = "TypesTest::Vector",
        .package = "OtherPackage",
        .role = Role::SharedStruct,
        .formerly = formerly,
    };

    INFO("a type claiming a registered name as its former name is refused");
    REQUIRE_FALSE(recording.registry.Register(renamed));
    REQUIRE(recording.diagnostics.size() == 1);

    INFO("the diagnostic names both types: " << recording.diagnostics.front());
    REQUIRE(Contains(recording.diagnostics.front(), "TypesTest::Vector"));
    REQUIRE(Contains(recording.diagnostics.front(), "TypesTest::Vec3"));

    INFO("the refused type's own canonical name was not added either");
    REQUIRE(recording.registry.Find("TypesTest::Vector") == nullptr);
}

TEST_CASE("A two-step base chain answers is-a both ways", "[types][registry]")
{
    TypeRegistry registry;
    INFO("setup: WeaponData then Sword register");
    REQUIRE(registry.Register(TypesTest::WeaponDataDescription));
    REQUIRE(registry.Register(TypesTest::SwordDescription));

    INFO("a Sword is a WeaponData");
    REQUIRE(registry.IsA("TypesTest::Sword", "TypesTest::WeaponData"));

    INFO("a WeaponData is not a Sword");
    REQUIRE_FALSE(registry.IsA("TypesTest::WeaponData", "TypesTest::Sword"));

    INFO("every type is itself");
    REQUIRE(registry.IsA("TypesTest::Sword", "TypesTest::Sword"));
    REQUIRE(registry.IsA("TypesTest::WeaponData", "TypesTest::WeaponData"));

    INFO("an unregistered name is nothing and has nothing");
    REQUIRE_FALSE(registry.IsA("TypesTest::Axe", "TypesTest::WeaponData"));
    REQUIRE_FALSE(registry.IsA("TypesTest::Sword", "TypesTest::Axe"));
}

TEST_CASE("A base chain of three answers is-a over every step", "[types][registry]")
{
    TypeRegistry registry;
    INFO("setup: WeaponData then Sword register");
    REQUIRE(registry.Register(TypesTest::WeaponDataDescription));
    REQUIRE(registry.Register(TypesTest::SwordDescription));

    const TypeDescription longsword{
        .name = "TypesTest::Longsword",
        .package = "FenrirTypesTests",
        .role = Role::DataAsset,
        .base = "TypesTest::Sword",
    };
    INFO("setup: Longsword, extending Sword, registers");
    REQUIRE(registry.Register(longsword));

    INFO("a Longsword is a WeaponData, two steps up");
    REQUIRE(registry.IsA("TypesTest::Longsword", "TypesTest::WeaponData"));

    INFO("a Sword is not a Longsword");
    REQUIRE_FALSE(registry.IsA("TypesTest::Sword", "TypesTest::Longsword"));
}

TEST_CASE("A type whose base is not registered is refused with a diagnostic", "[types][registry]")
{
    RecordingRegistry recording;

    INFO("a Sword registered before its base WeaponData is refused");
    REQUIRE_FALSE(recording.registry.Register(TypesTest::SwordDescription));
    REQUIRE(recording.diagnostics.size() == 1);

    INFO("the diagnostic names the type and its missing base: " << recording.diagnostics.front());
    REQUIRE(Contains(recording.diagnostics.front(), "TypesTest::Sword"));
    REQUIRE(Contains(recording.diagnostics.front(), "TypesTest::WeaponData"));

    INFO("the refused type is not registered");
    REQUIRE(recording.registry.Find("TypesTest::Sword") == nullptr);
}

TEST_CASE("A type that is not a data asset may not have a base", "[types][registry]")
{
    RecordingRegistry recording;
    INFO("setup: WeaponData registers");
    REQUIRE(recording.registry.Register(TypesTest::WeaponDataDescription));

    const TypeDescription componentWithBase{
        .name = "TypesTest::Derived",
        .package = "FenrirTypesTests",
        .role = Role::Component,
        .base = "TypesTest::WeaponData",
    };
    INFO("a component with a base is refused, with one diagnostic");
    REQUIRE_FALSE(recording.registry.Register(componentWithBase));
    REQUIRE(recording.diagnostics.size() == 1);
}

TEST_CASE("A data asset may extend only another data asset", "[types][registry]")
{
    RecordingRegistry recording;
    INFO("setup: Transform registers");
    REQUIRE(recording.registry.Register(TypesTest::TransformDescription));

    const TypeDescription dataAssetOnComponent{
        .name = "TypesTest::Odd",
        .package = "FenrirTypesTests",
        .role = Role::DataAsset,
        .base = "TypesTest::Transform",
    };
    INFO("a data asset extending a component is refused, with one diagnostic");
    REQUIRE_FALSE(recording.registry.Register(dataAssetOnComponent));
    REQUIRE(recording.diagnostics.size() == 1);
}

TEST_CASE("An owning kind in a role that does not admit one is refused", "[types][registry]")
{
    RecordingRegistry recording;

    static const Fenrir::FieldInfo fields[] = {{.name = "title", .type = {.kind = Fenrir::Kind::String}}};
    const TypeDescription component{
        .name = "TypesTest::Named",
        .package = "FenrirTypesTests",
        .role = Role::Component,
        .fields = fields,
    };

    INFO("a component holding a string is refused");
    REQUIRE_FALSE(recording.registry.Register(component));
    REQUIRE(recording.diagnostics.size() == 1);

    INFO("the diagnostic names the type, the field, the kind and the role: " << recording.diagnostics.front());
    REQUIRE(Contains(recording.diagnostics.front(), "TypesTest::Named"));
    REQUIRE(Contains(recording.diagnostics.front(), "title"));
    REQUIRE(Contains(recording.diagnostics.front(), "string"));
    REQUIRE(Contains(recording.diagnostics.front(), "component"));
}

namespace
{
    // A nested struct holding a string, walked by its own visit function, as a data asset's nested structs may be.
    struct Caption
    {
        std::string text;
    };

    const Fenrir::FieldInfo CaptionFields[] = {{.name = "text", .type = {.kind = Fenrir::Kind::String}}};

    void VisitCaption(void* instance, Fenrir::FieldVisitor& visitor)
    {
        visitor.Field(CaptionFields[0],
                      Fenrir::ValueRef(&static_cast<Caption*>(instance)->text, CaptionFields[0].type));
    }

    const TypeDescription CaptionDescription{
        .name = "TypesTest::Caption",
        .package = "FenrirTypesTests",
        .role = Role::Nested,
        .size = sizeof(Caption),
        .alignment = alignof(Caption),
        .fields = CaptionFields,
        .visit = VisitCaption,
    };

    const Fenrir::FieldInfo CaptionedFields[] = {
        {.name = "caption", .type = {.kind = Fenrir::Kind::Struct, .type = &CaptionDescription}}};
} // namespace

TEST_CASE("A nested struct holding an owning kind is judged in the role of the type holding it", "[types][registry]")
{
    RecordingRegistry recording;

    INFO("the nested struct itself registers");
    REQUIRE(recording.registry.Register(CaptionDescription));

    const TypeDescription component{
        .name = "TypesTest::CaptionedComponent",
        .package = "FenrirTypesTests",
        .role = Role::Component,
        .fields = CaptionedFields,
    };
    INFO("a component holding it by value is refused, naming the string it reaches");
    REQUIRE_FALSE(recording.registry.Register(component));
    REQUIRE(recording.diagnostics.size() == 1);
    REQUIRE(Contains(recording.diagnostics.front(), "string"));

    const TypeDescription dataAsset{
        .name = "TypesTest::CaptionedAsset",
        .package = "FenrirTypesTests",
        .role = Role::DataAsset,
        .fields = CaptionedFields,
    };
    INFO("a data asset holding it by value registers, its own field a struct it visits by offset");
    REQUIRE(recording.registry.Register(dataAsset));
}

TEST_CASE("A tagged union whose alternative reaches an owning kind is refused, in any role", "[types][registry]")
{
    RecordingRegistry recording;
    INFO("setup: Mobility and Caption register");
    REQUIRE(recording.registry.Register(TypesTest::MobilityDescription));
    REQUIRE(recording.registry.Register(CaptionDescription));

    static const Fenrir::Alternative alternatives[] = {
        {.tag = 0, .field = {.name = "m_count", .type = {.kind = Fenrir::Kind::U32}, .offset = 8}},
        {.tag = 7,
         .field = {.name = "m_caption",
                   .type = {.kind = Fenrir::Kind::Struct, .type = &CaptionDescription},
                   .offset = 8}},
    };
    const TypeDescription choice{
        .name = "TypesTest::Choice",
        .package = "FenrirTypesTests",
        .role = Role::Settings,
        .tag = {.name = "m_type",
                .type = {.kind = Fenrir::Kind::Enum, .type = &TypesTest::MobilityDescription, .unionTag = true}},
        .unionOffset = 8,
        .alternatives = alternatives,
    };

    INFO("an alternative is read at the union's offset, so a settings type's tagged union holds no string either");
    REQUIRE_FALSE(recording.registry.Register(choice));
    REQUIRE(recording.diagnostics.size() == 1);
    INFO("the diagnostic names the alternative and the kind: " << recording.diagnostics.front());
    REQUIRE(Contains(recording.diagnostics.front(), "m_caption"));
    REQUIRE(Contains(recording.diagnostics.front(), "string"));
}

TEST_CASE("A type with an owning field and no visit function is refused", "[types][registry]")
{
    RecordingRegistry recording;

    static const Fenrir::FieldInfo fields[] = {{.name = "title", .type = {.kind = Fenrir::Kind::String}}};
    const TypeDescription unvisitable{
        .name = "TypesTest::Unvisitable",
        .package = "FenrirTypesTests",
        .role = Role::DataAsset,
        .fields = fields,
    };

    INFO("an owning field has no offset to visit by, so the type must bring a visit function");
    REQUIRE_FALSE(recording.registry.Register(unvisitable));
    REQUIRE(recording.diagnostics.size() == 1);
    REQUIRE(Contains(recording.diagnostics.front(), "visit function"));
}

TEST_CASE("A type naming one name twice among its own names is refused", "[types][registry]")
{
    RecordingRegistry recording;

    static constexpr std::string_view formerly[] = {"TypesTest::Old", "TypesTest::Old"};
    const TypeDescription repeated{
        .name = "TypesTest::Repeated",
        .package = "FenrirTypesTests",
        .role = Role::SharedStruct,
        .formerly = formerly,
    };

    INFO("a former name given twice is refused, naming it");
    REQUIRE_FALSE(recording.registry.Register(repeated));
    REQUIRE(recording.diagnostics.size() == 1);
    REQUIRE(Contains(recording.diagnostics.front(), "TypesTest::Old"));
}

TEST_CASE("A version below 1 is refused", "[types][registry]")
{
    RecordingRegistry recording;
    const TypeDescription unversioned{
        .name = "TypesTest::Unversioned",
        .package = "FenrirTypesTests",
        .role = Role::SharedStruct,
        .version = 0,
    };

    INFO("version 0 is refused with a diagnostic");
    REQUIRE_FALSE(recording.registry.Register(unversioned));
    REQUIRE(recording.diagnostics.size() == 1);
}

TEST_CASE("Types are listed by role in registration order", "[types][registry]")
{
    TypeRegistry registry;
    INFO("setup: four types of three roles register");
    REQUIRE(registry.Register(TypesTest::Vec3Description));
    REQUIRE(registry.Register(TypesTest::TransformDescription));
    REQUIRE(registry.Register(TypesTest::WeaponDataDescription));
    REQUIRE(registry.Register(TypesTest::ProbeDescription));

    const std::vector<const TypeDescription*> components = registry.Types(Role::Component);
    INFO("the two components, in the order they registered");
    REQUIRE(components ==
            std::vector<const TypeDescription*>{&TypesTest::TransformDescription, &TypesTest::ProbeDescription});

    INFO("a role nothing registered lists nothing");
    REQUIRE(registry.Types(Role::Settings).empty());
}
