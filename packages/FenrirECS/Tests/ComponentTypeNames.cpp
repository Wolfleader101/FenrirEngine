// A component found by the name a file spells (ADR-0074): FenrirECS's registration hangs a ComponentType on each of its
// described components, and ComponentType::ForName and NameOf read it back through the registry alone.
#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/Generated/FenrirECSTypes.hpp"
#include "FenrirECS/TypeAttachments.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <optional>
#include <string_view>

using Fenrir::ComponentType;
using Fenrir::EntityList;
using Fenrir::TypeRegistry;

namespace
{
    // A registry holding FenrirECS's described types (and FenrirMath's, which its registration runs first).
    TypeRegistry EcsRegistry()
    {
        TypeRegistry registry;
        INFO("setup: FenrirECS's generated registration adds its types with no diagnostic");
        REQUIRE(Fenrir::Generated::RegisterFenrirECSTypes(registry));
        return registry;
    }

    // A component the registry has never heard of.
    struct Unregistered
    {
        int value = 0;
    };
} // namespace

TEST_CASE("ForName finds Transform as a ComponentType an entity accepts", "[component-type][types]")
{
    const TypeRegistry registry = EcsRegistry();
    EntityList entities;
    const auto entity = entities.CreateEntity().GetId();

    const std::optional<ComponentType> transform = ComponentType::ForName(registry, "Fenrir::Transform");
    INFO("FenrirECS's registration should hang a ComponentType on Fenrir::Transform");
    REQUIRE(transform.has_value());

    INFO("the entity takes a Transform through it");
    REQUIRE(entities.Add(*transform, entity));
    INFO("and the C++ type sees the component it added");
    REQUIRE(entities.Get(ComponentType::Native<Fenrir::Transform>(), entity) != nullptr);
}

TEST_CASE("ForName gives the same component as the C++ type's own ComponentType", "[component-type][types]")
{
    const TypeRegistry registry = EcsRegistry();
    EntityList entities;
    const auto entity = entities.CreateEntity().GetId();
    entities.Add(ComponentType::Native<Fenrir::Name>(), entity);

    const std::optional<ComponentType> name = ComponentType::ForName(registry, "Fenrir::Name");
    INFO("FenrirECS's registration should hang a ComponentType on Fenrir::Name");
    REQUIRE(name.has_value());

    INFO("a Name added through the C++ type is found through the named one");
    REQUIRE(entities.Has(*name, entity));
}

TEST_CASE("A component removed through its named ComponentType is gone for the C++ type", "[component-type][types]")
{
    const TypeRegistry registry = EcsRegistry();
    EntityList entities;
    const auto entity = entities.CreateEntity().GetId();
    entities.Add(ComponentType::Native<Fenrir::Name>(), entity);

    const std::optional<ComponentType> name = ComponentType::ForName(registry, "Fenrir::Name");
    INFO("FenrirECS's registration should hang a ComponentType on Fenrir::Name");
    REQUIRE(name.has_value());
    INFO("removing the Name through the named ComponentType should succeed");
    REQUIRE(entities.Remove(*name, entity));
    INFO("and the C++ type should then see no Name");
    REQUIRE(!entities.Has(ComponentType::Native<Fenrir::Name>(), entity));
}

TEST_CASE("NameOf gives a found component's name back", "[component-type][types]")
{
    const TypeRegistry registry = EcsRegistry();

    for (const std::string_view name : {"Fenrir::Name", "Fenrir::Relationship", "Fenrir::Transform"})
    {
        const std::optional<ComponentType> type = ComponentType::ForName(registry, name);
        INFO(name << " should be found by name");
        REQUIRE(type.has_value());
        INFO("NameOf should give " << name << " back");
        REQUIRE(ComponentType::NameOf(registry, *type) == name);
    }
}

TEST_CASE("NameOf names the C++ type's own ComponentType", "[component-type][types]")
{
    const TypeRegistry registry = EcsRegistry();

    INFO("a ComponentType made from the C++ type names the same component as the registered one");
    REQUIRE(ComponentType::NameOf(registry, ComponentType::Native<Fenrir::Relationship>()) == "Fenrir::Relationship");
}

TEST_CASE("An unknown name gives no ComponentType", "[component-type][types]")
{
    const TypeRegistry registry = EcsRegistry();

    INFO("no type is registered as Fenrir::NoSuchComponent");
    REQUIRE(!ComponentType::ForName(registry, "Fenrir::NoSuchComponent").has_value());
    INFO("Fenrir::Math::Vec3 is registered, but it is not a component");
    REQUIRE(!ComponentType::ForName(registry, "Fenrir::Math::Vec3").has_value());
}

TEST_CASE("NameOf gives no name for a component the registry does not hold", "[component-type][types]")
{
    const TypeRegistry registry = EcsRegistry();

    INFO("Unregistered is a native component no registration described");
    REQUIRE(!ComponentType::NameOf(registry, ComponentType::Native<Unregistered>()).has_value());
    INFO("a dynamic component's name is its DynamicComponentType's, not the registry's");
    REQUIRE(!ComponentType::NameOf(registry, ComponentType::Dynamic(Fenrir::DynamicComponentType("Test.Pair", 8, 4)))
                 .has_value());
}

TEST_CASE("An empty registry finds no component", "[component-type][types]")
{
    const TypeRegistry registry;

    INFO("nothing registered, so ForName finds no component");
    REQUIRE(!ComponentType::ForName(registry, "Fenrir::Transform").has_value());
    INFO("and NameOf names none");
    REQUIRE(!ComponentType::NameOf(registry, ComponentType::Native<Fenrir::Transform>()).has_value());
}
